#include "features/bluetooth/bluetooth_service.h"

#include "config.h"
#include "core/robot_settings.h"
#include "sdkconfig.h"
#include "application.h"
#include "assets/lang_config.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace kynangso {
namespace {
constexpr char kTag[] = "RobotBluetooth";
}  // namespace

#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
namespace {
constexpr size_t kMaxFrame = 768;
constexpr size_t kMaxDevices = 16;
constexpr int kBaudRate = 115200;

bool ContainsIgnoreCase(const std::string& value, const std::string& needle) {
    if (needle.empty())
        return false;
    auto it = std::search(
        value.begin(), value.end(), needle.begin(), needle.end(),
        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
    return it != value.end();
}
}  // namespace
#endif

BluetoothService::BluetoothService() {
#if !CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
    RobotSettings settings;
    auto extensions = settings.LoadExtensions();
    if (extensions.bluetooth) {
        extensions.bluetooth = false;
        const auto error = settings.SaveExtensions(extensions);
        if (error != ESP_OK)
            ESP_LOGW(kTag, "Could not clear stale Bluetooth extension setting: %s",
                     esp_err_to_name(error));
    }
    status_.message = "Hỗ trợ Bluetooth D1 Mini chưa được biên dịch trong firmware này.";
    return;
#else
    if (!RobotSettings().LoadExtensions().bluetooth) {
        status_.message =
            "Bluetooth đang tắt; bật trong cấu hình phần mở rộng và khởi động lại robot.";
        return;
    }
    constexpr int tx = CONFIG_KYNANGSO_ROBOT_BLUETOOTH_UART_TX_GPIO < 0
                           ? 10
                           : CONFIG_KYNANGSO_ROBOT_BLUETOOTH_UART_TX_GPIO;
    constexpr int rx = CONFIG_KYNANGSO_ROBOT_BLUETOOTH_UART_RX_GPIO < 0
                           ? 21
                           : CONFIG_KYNANGSO_ROBOT_BLUETOOTH_UART_RX_GPIO;
    if (tx < 0 || rx < 0 || tx == rx) {
        status_.message = "Chưa cấu hình đúng chân UART cho mô-đun Bluetooth.";
        ESP_LOGI(kTag, "%s", status_.message.c_str());
        return;
    }

    uart_config_t config = {
        .baud_rate = kBaudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(kUart, 2048, 0, 0, nullptr, 0) != ESP_OK ||
        uart_param_config(kUart, &config) != ESP_OK ||
        uart_set_pin(kUart, static_cast<gpio_num_t>(tx), static_cast<gpio_num_t>(rx),
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(kTag, "Cannot initialize UART2 for D1 Mini");
        return;
    }
    uart_ready_ = true;
    status_.enabled = true;
    status_.message = "Đang chờ kết nối D1 Mini qua UART.";
    if (xTaskCreate(ReceiveTask, "robot_bt_uart", 4096, this, 5, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Cannot start Bluetooth UART receiver");
        uart_driver_delete(kUart);
        uart_ready_ = false;
        status_.enabled = false;
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    SendCommand("STATUS", nullptr);
    SendCommand("AUTO_CONNECT", nullptr);
#endif
}

BluetoothService::~BluetoothService() {
#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
    if (uart_ready_)
        uart_driver_delete(kUart);
#endif
}

#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
void BluetoothService::ReceiveTask(void* arg) {
    static_cast<BluetoothService*>(arg)->ReceiveLoop();
}

void BluetoothService::ReceiveLoop() {
    char frame[kMaxFrame];
    size_t used = 0;
    uint8_t bytes[64];
    int64_t next_keepalive_us = esp_timer_get_time() + 15000000;
    while (true) {
        const int count = uart_read_bytes(kUart, bytes, sizeof(bytes), pdMS_TO_TICKS(100));
        for (int i = 0; i < count; ++i) {
            const char ch = static_cast<char>(bytes[i]);
            if (ch == '\n' || ch == '\r') {
                if (used != 0) {
                    frame[used] = '\0';
                    HandleFrame(frame);
                    used = 0;
                }
            } else if (used + 1 < sizeof(frame)) {
                frame[used++] = ch;
            } else {
                used = 0;
            }
        }
        const int64_t now = esp_timer_get_time();
        if (now >= next_keepalive_us) {
            SendCommand("KEEPALIVE", nullptr);
            next_keepalive_us = now + 15000000;
        }
    }
}

void BluetoothService::HandleFrame(const char* frame) {
    cJSON* root = cJSON_Parse(frame);
    if (root == nullptr)
        return;
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && std::strcmp(type->valuestring, "state") == 0) {
        const cJSON* connected = cJSON_GetObjectItemCaseSensitive(root, "connected");
        const cJSON* streaming = cJSON_GetObjectItemCaseSensitive(root, "streaming");
        const cJSON* scanning = cJSON_GetObjectItemCaseSensitive(root, "scanning");
        const cJSON* switching = cJSON_GetObjectItemCaseSensitive(root, "switching");
        const cJSON* auto_connected = cJSON_GetObjectItemCaseSensitive(root, "auto_connected");
        const cJSON* current = cJSON_GetObjectItemCaseSensitive(root, "current");
        const cJSON* target = cJSON_GetObjectItemCaseSensitive(root, "target");
        const cJSON* message = cJSON_GetObjectItemCaseSensitive(root, "message");
        std::string announcement_name;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_frame_us_ = esp_timer_get_time();
            status_.online = true;
            status_.connected = cJSON_IsTrue(connected);
            status_.streaming = cJSON_IsTrue(streaming);
            status_.scanning = cJSON_IsTrue(scanning);
            status_.switching = cJSON_IsTrue(switching);
            if (cJSON_IsString(current))
                status_.current = current->valuestring;
            if (cJSON_IsString(target))
                status_.target = target->valuestring;
            if (cJSON_IsString(message))
                status_.message = message->valuestring;
            if (cJSON_IsTrue(auto_connected))
                announcement_name = status_.current;
        }
        if (cJSON_IsTrue(auto_connected)) {
            Application::GetInstance().Schedule([announcement_name]() {
                auto& app = Application::GetInstance();
                const std::string text = announcement_name.empty()
                                             ? "Đã tự kết nối lại loa Bluetooth."
                                             : "Đã tự kết nối lại loa: " + announcement_name;
                app.Alert("Loa Bluetooth", text.c_str(), "happy",
                          Lang::Sounds::OGG_ROBOT_STARTING);
            });
        }
    } else if (cJSON_IsString(type) && std::strcmp(type->valuestring, "device") == 0) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
        const cJSON* address = cJSON_GetObjectItemCaseSensitive(root, "address");
        const cJSON* rssi = cJSON_GetObjectItemCaseSensitive(root, "rssi");
        if (cJSON_IsString(name) && cJSON_IsString(address) && cJSON_IsNumber(rssi)) {
            BluetoothDevice device{name->valuestring, address->valuestring, rssi->valueint};
            std::lock_guard<std::mutex> lock(mutex_);
            last_frame_us_ = esp_timer_get_time();
            status_.online = true;
            auto found =
                std::find_if(status_.devices.begin(), status_.devices.end(),
                             [&](const auto& item) { return item.address == device.address; });
            if (found != status_.devices.end())
                *found = std::move(device);
            else if (status_.devices.size() < kMaxDevices)
                status_.devices.push_back(std::move(device));
        }
    }
    cJSON_Delete(root);
}

BluetoothStatus BluetoothService::GetStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto snapshot = status_;
    snapshot.online = status_.online && esp_timer_get_time() - last_frame_us_ < 3000000;
    if (!snapshot.online) {
        snapshot.connected = false;
        snapshot.scanning = false;
        snapshot.switching = false;
        snapshot.streaming = false;
    }
    return snapshot;
}

bool BluetoothService::SendCommand(const char* command, std::string* error) {
    if (!uart_ready_) {
        if (error)
            *error = "Kết nối UART với D1 Mini chưa sẵn sàng.";
        return false;
    }
    std::string line(command);
    line.push_back('\n');
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (uart_write_bytes(kUart, line.data(), line.size()) != static_cast<int>(line.size())) {
        if (error)
            *error = "Không gửi được lệnh tới D1 Mini.";
        return false;
    }
    return true;
}

bool BluetoothService::Scan(std::string* error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!status_.enabled) {
            if (error)
                *error = status_.message;
            return false;
        }
        if (!status_.online || esp_timer_get_time() - last_frame_us_ >= 3000000) {
            if (error)
                *error = "D1 Mini đang ngoại tuyến.";
            return false;
        }
        status_.devices.clear();
        status_.scanning = true;
        status_.message = "Đang quét loa Bluetooth.";
    }
    if (!SendCommand("SCAN", error)) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.scanning = false;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++status_.operation_id;
        status_.operation = 1;
    }
    return true;
}

bool BluetoothService::ConnectByName(const std::string& name, std::string* error) {
    if (name.empty() || name.size() > 64) {
        if (error)
            *error = "Tên loa đang trống hoặc quá dài.";
        return false;
    }
    BluetoothDevice match;
    size_t matches = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!status_.enabled) {
            if (error)
                *error = status_.message;
            return false;
        }
        if (!status_.online) {
            if (error)
                *error = "Chưa có phản hồi từ D1 Mini.";
            return false;
        }
        for (const auto& device : status_.devices) {
            if (ContainsIgnoreCase(device.name, name)) {
                match = device;
                ++matches;
            }
        }
    }
    if (matches != 1) {
        if (error)
            *error = matches == 0 ? "Không tìm thấy tên loa trong kết quả quét gần nhất."
                                  : "Tên khớp với nhiều loa; hãy chọn một thiết bị cụ thể.";
        return false;
    }
    return ConnectByAddress(match.address, error);
}

bool BluetoothService::ConnectByAddress(const std::string& address, std::string* error) {
    BluetoothDevice match;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!status_.enabled) {
            if (error)
                *error = status_.message;
            return false;
        }
        if (!status_.online || esp_timer_get_time() - last_frame_us_ >= 3000000) {
            if (error)
                *error = "D1 Mini đang ngoại tuyến.";
            return false;
        }
        const auto found = std::find_if(status_.devices.begin(), status_.devices.end(),
                                        [&](const auto& item) { return item.address == address; });
        if (found == status_.devices.end()) {
            if (error)
                *error = "Thiết bị không có trong kết quả quét gần nhất.";
            return false;
        }
        match = *found;
    }
    const bool sent = SendCommand(("CONNECT " + match.address).c_str(), error);
    if (sent) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++status_.operation_id;
        status_.operation = 2;
        status_.switching = true;
        status_.target = match.name;
        status_.target_address = match.address;
        status_.message = "Đang chuyển sang loa đã chọn.";
    }
    return sent;
}

bool BluetoothService::Disconnect(std::string* error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!status_.enabled) {
            if (error)
                *error = status_.message;
            return false;
        }
        if (!status_.online || esp_timer_get_time() - last_frame_us_ >= 3000000) {
            if (error)
                *error = "D1 Mini đang ngoại tuyến.";
            return false;
        }
    }
    if (!SendCommand("DISCONNECT", error))
        return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++status_.operation_id;
        status_.operation = 3;
    }
    return true;
}
#else
BluetoothStatus BluetoothService::GetStatus() const { return status_; }

bool BluetoothService::Scan(std::string* error) {
    if (error)
        *error = status_.message;
    return false;
}

bool BluetoothService::ConnectByName(const std::string&, std::string* error) {
    if (error)
        *error = status_.message;
    return false;
}

bool BluetoothService::ConnectByAddress(const std::string&, std::string* error) {
    if (error)
        *error = status_.message;
    return false;
}

bool BluetoothService::Disconnect(std::string* error) {
    if (error)
        *error = status_.message;
    return false;
}
#endif

}  // namespace kynangso
