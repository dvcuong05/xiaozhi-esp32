#include "device_bridge_client.h"

#include <nvs.h>
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
#include <freertos/semphr.h>
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

bool DeviceBridgeClient::ValidateConfig(const Config& config) {
    const auto& uri = config.uri;
    if (uri.size() < 7 || uri.size() > 256 || uri.compare(0, 6, "wss://") != 0 ||
        config.device_id.empty() || config.device_id.size() > 64 || config.token.size() < 32 ||
        config.token.size() > 128)
        return false;
    for (unsigned char c : uri) {
        if (c <= 32 || c >= 127 || c == '@' || c == '#' || c == '?' || c == '\\' || c == '"')
            return false;
    }
    const auto authority = uri.substr(6, uri.find('/', 6) - 6);
    const auto colon = authority.find(':');
    const auto host = authority.substr(0, colon);
    if (host.empty() || host.front() == '.' || host.front() == '-' || host.back() == '.' ||
        host.back() == '-')
        return false;
    for (unsigned char c : host) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-'))
            return false;
    }
    if (colon != std::string::npos) {
        const auto port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5)
            return false;
        unsigned number = 0;
        for (char c : port) {
            if (c < '0' || c > '9')
                return false;
            number = number * 10 + c - '0';
        }
        if (number == 0 || number > 65535)
            return false;
    }
    for (unsigned char c : config.device_id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return false;
    }
    for (unsigned char c : config.token) {
        if (c < 33 || c > 126)
            return false;
    }
    return true;
}

bool DeviceBridgeClient::ParseConfig(const char* json, Config* config) {
    // Reject escaped Unicode/NUL rather than silently accepting a truncated C string.
    if (!json || !config || strlen(json) > 768)
        return false;
    for (const char* p = json; *p; ++p) {
        if (*p == '\\') {
            ++p;
            if (!*p || *p == 'u')
                return false;
        }
    }
    cJSON* root = cJSON_ParseWithOpts(json, nullptr, true);
    bool valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 3;
    Config parsed;
    const char* keys[] = {"uri", "device_id", "token"};
    std::string* values[] = {&parsed.uri, &parsed.device_id, &parsed.token};
    for (size_t i = 0; valid && i < 3; ++i) {
        auto* item = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
        valid = cJSON_IsString(item);
        if (valid)
            *values[i] = item->valuestring;
    }
    cJSON_Delete(root);
    if (!valid || !ValidateConfig(parsed))
        return false;
    *config = std::move(parsed);
    return true;
}

bool DeviceBridgeClient::SaveConfig(const Config& config) {
    if (!ValidateConfig(config))
        return false;
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return false;
    bool valid = cJSON_AddStringToObject(root, "uri", config.uri.c_str()) &&
                 cJSON_AddStringToObject(root, "device_id", config.device_id.c_str()) &&
                 cJSON_AddStringToObject(root, "token", config.token.c_str());
    char* encoded = valid ? cJSON_PrintUnformatted(root) : nullptr;
    cJSON_Delete(root);
    if (!encoded)
        return false;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("mcp_bridge", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        // One value prevents mixing credentials across interrupted provisioning attempts.
        result = nvs_set_str(handle, "config", encoded);
        if (result == ESP_OK)
            result = nvs_commit(handle);
        nvs_close(handle);
    }
    cJSON_free(encoded);
    return result == ESP_OK;
}

struct DeviceBridgeClient::RpcRequest {
    std::string id, payload, result, error;
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    bool sent = false;
    bool completed = false;
    ~RpcRequest() {
        if (done)
            vSemaphoreDelete(done);
    }
};

bool DeviceBridgeClient::Request(const std::string& payload, std::string* result,
                                 std::string* error, uint32_t timeout_ms) {
    auto reject = [error](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    if (!IsConnected())
        return reject("MCP server chưa kết nối. Vui lòng thử lại sau.");
    if (payload.empty() || payload.size() > 4096 || timeout_ms > 30000 || timeout_ms < 100)
        return reject("Yêu cầu dịch vụ không hợp lệ.");
    auto pending = std::make_shared<RpcRequest>();
    if (!pending->done)
        return reject("Không đủ bộ nhớ cho yêu cầu dịch vụ.");
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        if (request_)
            return reject("Đang xử lý một yêu cầu dịch vụ khác.");
        if (!IsConnected())
            return reject("MCP server vừa mất kết nối.");
        pending->id = "device_" + std::to_string(++request_sequence_);
        pending->payload = payload;
        request_ = pending;
        if (wake_queue_) {
            BridgeFrame wake{nullptr, 0};
            xQueueSend(static_cast<QueueHandle_t>(wake_queue_), &wake, 0);
        }
    }
    const bool completed = xSemaphoreTake(pending->done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    std::lock_guard<std::mutex> lock(request_mutex_);
    if (request_ == pending)
        request_.reset();
    if (!completed)
        return reject("MCP server trả lời quá lâu. Hãy tải lại trạng thái trước khi thử lại.");
    if (!pending->error.empty()) {
        if (error)
            *error = pending->error;
        return false;
    }
    if (result)
        *result = pending->result;
    return true;
}

void DeviceBridgeClient::FailRequest(const char* error) {
    std::lock_guard<std::mutex> lock(request_mutex_);
    if (request_ && !request_->completed) {
        request_->completed = true;
        request_->error = error;
        xSemaphoreGive(request_->done);
    }
}

void DeviceBridgeClient::SendRequest(void* context) {
    std::shared_ptr<RpcRequest> pending;
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        if (!request_ || request_->sent)
            return;
        pending = request_;
        pending->sent = true;
    }
    cJSON* envelope = cJSON_CreateObject();
    cJSON_AddStringToObject(envelope, "type", "request");
    cJSON_AddStringToObject(envelope, "id", pending->id.c_str());
    cJSON_AddItemToObject(envelope, "payload", cJSON_Parse(pending->payload.c_str()));
    auto encoded = PrintJson(envelope);
    cJSON_Delete(envelope);
    if (!static_cast<WebSocket*>(context)->Send(encoded))
        FailRequest("Không gửi được yêu cầu tới MCP server.");
}

DeviceBridgeClient::DeviceBridgeClient(CommandHandler command_handler)
    : command_handler_(std::move(command_handler)) {}

void DeviceBridgeClient::Start() {
    if (!kDeviceBridgeEnabled || started_) {
        return;
    }
    nvs_handle_t handle;
    bool configured = false;
    if (nvs_open("mcp_bridge", NVS_READONLY, &handle) == ESP_OK) {
        char json[769];
        size_t length = sizeof(json);
        if (nvs_get_str(handle, "config", json, &length) == ESP_OK)
            configured = ParseConfig(json, &config_);
        nvs_close(handle);
    }
    // Move the previously documented VPS endpoint to the public TLS hostname
    // without rotating or printing the robot credential stored in NVS.
    constexpr char kPreviousBridgeUri[] = "wss://103.77.242.31:18443/device/bridge";
    if (configured && config_.uri == kPreviousBridgeUri) {
        config_.uri = "wss://robot.kynangso.com/device/bridge";
        if (SaveConfig(config_)) {
            ESP_LOGI(TAG, "Migrated device bridge to the public TLS domain");
        } else {
            ESP_LOGW(TAG, "Using the public TLS domain for this boot; NVS migration failed");
        }
    }
    if (!configured) {
        config_ = {kBridgeUri, kBridgeDeviceId, kBridgeToken};
        configured = ValidateConfig(config_);
    }
    if (!configured) {
        ESP_LOGI(TAG, "WSS bridge awaiting local provisioning");
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
    const std::string authorization = std::string("Bearer ") + config_.token;
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
        socket->SetHeader("X-Device-Id", config_.device_id.c_str());
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
        auto connected = socket->Connect(config_.uri.c_str());
        if (!connected) {
            ESP_LOGW(TAG, "WSS connect failed: %s", connected.error().ToString().c_str());
        } else {
            connected_.store(
                socket->IsConnected() && !disconnected->load() && !inbox->failed.load(),
                std::memory_order_release);
            ESP_LOGI(TAG, "MCP WSS bridge connected");
            {
                std::lock_guard<std::mutex> lock(request_mutex_);
                wake_queue_ = inbox->queue;
            }
            TickType_t last_ping = xTaskGetTickCount();
            while (socket->IsConnected() && !disconnected->load() && !inbox->failed.load()) {
                SendRequest(socket.get());
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
                    if (frame.length != 0 && socket->IsConnected() && !disconnected->load() &&
                        !inbox->failed.load()) {
                        // JSON parsing, board I/O and ACKs run on this task's
                        // 12 KiB stack, never on the 4 KiB ssl_receive task.
                        HandleMessage(socket.get(), message.get(), frame.length, false);
                    }
                }
            }
        }

        connected_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(request_mutex_);
            wake_queue_ = nullptr;
        }
        FailRequest("Mất kết nối MCP server trong lúc xử lý yêu cầu.");
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
    if (cJSON_IsString(message_type) && strcmp(message_type->valuestring, "response") == 0 &&
        cJSON_IsString(request_id) && request_id->valuestring) {
        std::lock_guard<std::mutex> lock(request_mutex_);
        if (request_ && !request_->completed && request_->id == request_id->valuestring) {
            request_->completed = true;
            const auto* ok = cJSON_GetObjectItemCaseSensitive(envelope, "ok");
            const auto* result = cJSON_GetObjectItemCaseSensitive(envelope, "result_json");
            const auto* error = cJSON_GetObjectItemCaseSensitive(envelope, "error");
            if (cJSON_IsTrue(ok) && cJSON_IsString(result) && result->valuestring &&
                std::strlen(result->valuestring) <= 8192)
                request_->result = result->valuestring;
            else
                request_->error =
                    cJSON_IsString(error) ? error->valuestring : "Yêu cầu dịch vụ thất bại.";
            xSemaphoreGive(request_->done);
        }
        cJSON_Delete(envelope);
        return;
    }
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
