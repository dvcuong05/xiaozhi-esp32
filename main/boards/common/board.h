#ifndef BOARD_H
#define BOARD_H

#include <http.h>
#include <mqtt.h>
#include <network_interface.h>
#include <udp.h>
#include <web_socket.h>
#include <functional>
#include <string>
#include <string_view>

#include "assets.h"
#include "backlight.h"
#include "boot_sequence.h"
#include "camera.h"
#include "led/led.h"

/**
 * Network events for unified callback
 */
enum class NetworkEvent {
    Scanning,             // Network is scanning (WiFi scanning, etc.)
    Connecting,           // Network is connecting (data: SSID/network name)
    Connected,            // Network connected successfully (data: SSID/network name)
    Disconnected,         // Network disconnected
    WifiConfigModeEnter,  // Entered WiFi configuration mode
    WifiConfigModeExit,   // Exited WiFi configuration mode
    // Cellular modem specific events
    ModemDetecting,        // Detecting modem (baud rate, module type)
    ModemErrorNoSim,       // No SIM card detected
    ModemErrorRegDenied,   // Network registration denied
    ModemErrorInitFailed,  // Modem initialization failed
    ModemErrorTimeout      // Operation timeout
};

// Power save level enumeration
enum class PowerSaveLevel {
    LOW_POWER,    // Maximum power saving (lowest power consumption)
    BALANCED,     // Medium power saving (balanced)
    PERFORMANCE,  // No power saving (maximum power consumption / full performance)
};

// Network event callback type (event, data)
// data contains additional info like SSID for Connecting/Connected events
using NetworkEventCallback = std::function<void(NetworkEvent event, const std::string& data)>;

void* create_board();
class AudioCodec;
class Display;
class Board {
private:
    Board(const Board&) = delete;             // 禁用拷贝构造函数
    Board& operator=(const Board&) = delete;  // 禁用赋值操作

protected:
    Board();
    std::string GenerateUuid();

    // 软件生成的设备唯一标识
    std::string uuid_;

public:
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    virtual ~Board() = default;
    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight* GetBacklight() { return nullptr; }
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual bool GetTemperature(float& esp32temp);
    virtual Display* GetDisplay();
    // Optional hook after the display/audio stack is ready and before networking starts.
    virtual void OnApplicationReady() {}
    // Optional board-local consumer for the shared boot event stream.
    virtual void OnBootEvent(const BootEvent& event) { (void)event; }
    virtual BootAudioConfig GetBootAudioConfig() { return {}; }
    // Optional board-provided boot sounds. Empty views disable the sound on boards
    // that do not opt into a boot presentation.
    virtual std::string_view GetBootSound(BootStage stage) {
        (void)stage;
        return {};
    }
    virtual Camera* GetCamera();
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void StartNetwork() = 0;
    virtual void SetNetworkEventCallback(NetworkEventCallback callback) { (void)callback; }
    // Optional handler for board-local commands arriving over the authenticated device bridge.
    virtual bool HandleDeviceBridgeCommand(const std::string& payload_json,
                                           std::string* result_json, std::string* error) {
        (void)payload_json;
        (void)result_json;
        if (error != nullptr)
            *error = "unsupported board command";
        return false;
    }
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging);
    virtual std::string GetSystemInfoJson();
    virtual void SetPowerSaveLevel(PowerSaveLevel level) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
    void* create_board() { return new BOARD_CLASS_NAME(); }

#endif  // BOARD_H
