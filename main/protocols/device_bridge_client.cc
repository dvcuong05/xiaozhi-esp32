#include "device_bridge_client.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <web_socket.h>

#include "board.h"

#define TAG "DeviceBridge"

namespace {

constexpr size_t kBridgeReceiveBufferSize = 12288;
constexpr size_t kBridgeQueueDepth = 2;
constexpr TickType_t kReconnectDelay = pdMS_TO_TICKS(5000);
constexpr TickType_t kPingInterval = pdMS_TO_TICKS(20000);
constexpr TickType_t kPongTimeout = pdMS_TO_TICKS(50000);
// TLS handshakes use a deep esp-tls/mbedTLS call chain; 4 KB overflows before
// the WebSocket bridge can finish connecting.
constexpr uint32_t kTaskStackSize = 12288;

struct BridgeFrame {
    char* data;
    size_t length;
};

// Each connection owns its inbox. The socket callback can outlive Run's local
// reference, so keep it alive until the transport releases its callbacks.
struct BridgeInbox {
    QueueHandle_t queue = xQueueCreate(kBridgeQueueDepth, sizeof(BridgeFrame));
    std::atomic_bool failed{false};

    ~BridgeInbox() {
        if (queue != nullptr) {
            BridgeFrame frame;
            while (xQueueReceive(queue, &frame, 0) == pdTRUE) {
                delete[] frame.data;
            }
            vQueueDelete(queue);
        }
    }

    void Push(const char* data, size_t length) {
        if (failed.load()) {
            return;
        }
        std::unique_ptr<char[]> copy(new (std::nothrow) char[length]);
        if (copy != nullptr) {
            std::memcpy(copy.get(), data, length);
            BridgeFrame frame{copy.get(), length};
            if (xQueueSend(queue, &frame, 0) == pdTRUE) {
                copy.release();
                return;
            }
        }
        // Reconnect rather than retain an unbounded backlog or run a command
        // on the TLS receive stack when the worker cannot accept it.
        failed.store(true);
        ESP_LOGW(TAG, "WSS receive queue full or allocation failed; reconnecting");
    }
};

#if defined(CONFIG_XIAOZHI_MCP_DEVICE_WSS_ENABLE) && CONFIG_XIAOZHI_MCP_DEVICE_WSS_ENABLE
constexpr bool kDeviceBridgeEnabled = true;
constexpr char kBridgeUri[] = CONFIG_XIAOZHI_MCP_DEVICE_WSS_URI;
constexpr char kBridgeDeviceId[] = CONFIG_XIAOZHI_MCP_DEVICE_WSS_DEVICE_ID;
constexpr char kBridgeToken[] = CONFIG_XIAOZHI_MCP_DEVICE_WSS_TOKEN;
#else
constexpr bool kDeviceBridgeEnabled = false;
constexpr char kBridgeUri[] = "";
constexpr char kBridgeDeviceId[] = "";
constexpr char kBridgeToken[] = "";
#endif

std::string PrintJson(cJSON* value) {
    if (value == nullptr) {
        return "{}";
    }
    char* encoded = cJSON_PrintUnformatted(value);
    std::string result = encoded == nullptr ? "{}" : encoded;
    cJSON_free(encoded);
    return result;
}

}  // namespace

DeviceBridgeClient::DeviceBridgeClient(CommandHandler command_handler)
    : command_handler_(std::move(command_handler)) {}

void DeviceBridgeClient::Start() {
    if (!kDeviceBridgeEnabled || started_) {
        return;
    }
    if (strncmp(kBridgeUri, "wss://", 6) != 0 || kBridgeDeviceId[0] == '\0' ||
        strlen(kBridgeToken) < 32) {
        ESP_LOGE(TAG, "WSS config needs a wss:// URL, device ID, and token of at least 32 chars");
        return;
    }
    started_ = true;
    if (xTaskCreate(TaskEntry, "device_bridge", kTaskStackSize, this, 4, nullptr) != pdPASS) {
        started_ = false;
        ESP_LOGE(TAG, "Failed to create WSS bridge task");
    }
}

void DeviceBridgeClient::TaskEntry(void* context) {
    static_cast<DeviceBridgeClient*>(context)->Run();
}

void DeviceBridgeClient::Run() {
    const std::string authorization = std::string("Bearer ") + kBridgeToken;
    while (true) {
        auto* network = Board::GetInstance().GetNetwork();
        auto socket = network == nullptr ? nullptr : network->CreateWebSocket(2);
        if (socket == nullptr) {
            ESP_LOGW(TAG, "Network is not ready for the WSS bridge; retrying shortly");
            vTaskDelay(kReconnectDelay);
            continue;
        }

        auto inbox = std::make_shared<BridgeInbox>();
        if (inbox->queue == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate WSS command queue");
            vTaskDelay(kReconnectDelay);
            continue;
        }

        socket->SetReceiveBufferSize(kBridgeReceiveBufferSize);
        socket->SetHeader("Authorization", authorization.c_str());
        socket->SetHeader("X-Device-Id", kBridgeDeviceId);
        auto disconnected = std::make_shared<std::atomic_bool>(false);
        auto last_pong = std::make_shared<std::atomic<TickType_t>>(xTaskGetTickCount());
        socket->OnData([inbox](const char* data, size_t length, bool binary) {
            if (!binary && data != nullptr && length != 0 && length <= kBridgeReceiveBufferSize) {
                inbox->Push(data, length);
            }
        });
        socket->OnDisconnected([this, disconnected]() {
            disconnected->store(true);
            connected_.store(false, std::memory_order_release);
        });
        socket->OnError([this, disconnected](const NetworkError& error) {
            ESP_LOGW(TAG, "WSS transport error: %s", error.ToString().c_str());
            disconnected->store(true);
            connected_.store(false, std::memory_order_release);
        });
        socket->OnPong([last_pong](const char*, size_t) { last_pong->store(xTaskGetTickCount()); });

        ESP_LOGI(TAG, "Connecting to the MCP WSS bridge");
        auto connected = socket->Connect(kBridgeUri);
        if (!connected) {
            ESP_LOGW(TAG, "WSS connect failed: %s", connected.error().ToString().c_str());
        } else {
            connected_.store(socket->IsConnected() && !disconnected->load() &&
                                 !inbox->failed.load(),
                             std::memory_order_release);
            ESP_LOGI(TAG, "MCP WSS bridge connected");
            TickType_t last_ping = xTaskGetTickCount();
            while (socket->IsConnected() && !disconnected->load() && !inbox->failed.load()) {
                const TickType_t now = xTaskGetTickCount();
                if (now - last_ping >= kPingInterval) {
                    socket->Ping();
                    last_ping = now;
                }
                if (now - last_pong->load() >= kPongTimeout) {
                    ESP_LOGW(TAG, "WSS bridge heartbeat timed out");
                    break;
                }
                BridgeFrame frame;
                if (xQueueReceive(inbox->queue, &frame, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    std::unique_ptr<char[]> message(frame.data);
                    if (socket->IsConnected() && !disconnected->load() && !inbox->failed.load()) {
                        // JSON parsing, board I/O and ACKs run on this task's
                        // 12 KiB stack, never on the 4 KiB ssl_receive task.
                        HandleMessage(socket.get(), message.get(), frame.length, false);
                    }
                }
            }
        }

        connected_.store(false, std::memory_order_release);
        socket->Close();
        socket.reset();
        inbox.reset();
        ESP_LOGI(TAG, "WSS bridge disconnected; retrying in 5 seconds");
        vTaskDelay(kReconnectDelay);
    }
}

void DeviceBridgeClient::HandleMessage(void* socket_context, const char* data, size_t length,
                                       bool binary) {
    if (binary || data == nullptr || length == 0 || length > kBridgeReceiveBufferSize) {
        return;
    }
    auto* socket = static_cast<WebSocket*>(socket_context);
    cJSON* envelope = cJSON_ParseWithLength(data, length);
    if (envelope == nullptr) {
        ESP_LOGW(TAG, "Ignoring malformed WSS bridge message");
        return;
    }

    const cJSON* message_type = cJSON_GetObjectItemCaseSensitive(envelope, "type");
    const cJSON* request_id = cJSON_GetObjectItemCaseSensitive(envelope, "id");
    const cJSON* payload = cJSON_GetObjectItemCaseSensitive(envelope, "payload");
    if (!cJSON_IsString(message_type) || strcmp(message_type->valuestring, "command") != 0 ||
        !cJSON_IsString(request_id) || request_id->valuestring == nullptr ||
        strlen(request_id->valuestring) == 0 || strlen(request_id->valuestring) > 64 ||
        !cJSON_IsObject(payload)) {
        cJSON_Delete(envelope);
        return;
    }

    char* command_json = cJSON_PrintUnformatted(payload);
    std::string result_json;
    std::string error;
    const bool accepted =
        command_json != nullptr && command_handler_(command_json, &result_json, &error);
    cJSON_free(command_json);

    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "ack");
    cJSON_AddStringToObject(response, "id", request_id->valuestring);
    cJSON_AddBoolToObject(response, "ok", accepted);
    if (accepted) {
        cJSON* result = cJSON_ParseWithLength(result_json.c_str(), result_json.size());
        cJSON_AddItemToObject(response, "result",
                              cJSON_IsObject(result) ? result : cJSON_CreateObject());
        if (result != nullptr && !cJSON_IsObject(result)) {
            cJSON_Delete(result);
        }
    } else {
        cJSON_AddStringToObject(response, "error",
                                error.empty() ? "invalid command" : error.c_str());
    }
    const std::string encoded = PrintJson(response);
    cJSON_Delete(response);
    cJSON_Delete(envelope);

    if (!socket->Send(encoded)) {
        ESP_LOGW(TAG, "Failed to acknowledge WSS bridge command");
    }
}
