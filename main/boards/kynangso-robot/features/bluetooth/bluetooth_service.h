#pragma once

#include "sdkconfig.h"

#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
#include <driver/uart.h>
#include <mutex>
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace kynangso {

struct BluetoothDevice {
    std::string name;
    std::string address;
    int rssi = 0;
};

struct BluetoothStatus {
    uint32_t operation_id = 0;
    int operation = 0;  // 1 scan, 2 connect, 3 disconnect
    bool enabled = false;
    bool online = false;
    bool connected = false;
    bool streaming = false;
    bool scanning = false;
    bool switching = false;
    std::string current;
    std::string target;
    std::string target_address;
    std::string message;
    std::vector<BluetoothDevice> devices;
};

// Single control path shared by the robot web server and MCP tools.
class BluetoothService {
public:
    BluetoothService();
    ~BluetoothService();
    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;

    BluetoothStatus GetStatus() const;
    bool Scan(std::string* error = nullptr);
    bool ConnectByName(const std::string& name, std::string* error = nullptr);
    bool ConnectByAddress(const std::string& address, std::string* error = nullptr);
    bool Disconnect(std::string* error = nullptr);

private:
#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
    static void ReceiveTask(void* arg);
    void ReceiveLoop();
    void HandleFrame(const char* frame);
    bool SendCommand(const char* command, std::string* error);

    static constexpr uart_port_t kUart = UART_NUM_2;
    mutable std::mutex mutex_;
    std::mutex tx_mutex_;
    bool uart_ready_ = false;
    int64_t last_frame_us_ = 0;
#endif
    BluetoothStatus status_;
};

}  // namespace kynangso
