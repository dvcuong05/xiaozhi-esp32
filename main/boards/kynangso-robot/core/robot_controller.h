#ifndef KYNANGSO_ROBOT_CONTROLLER_H_
#define KYNANGSO_ROBOT_CONTROLLER_H_

#include "core/robot_settings.h"
#include "features/bluetooth/bluetooth_service.h"
#include "features/face/face_theme_service.h"
#include "features/motion/robot_drive.h"
#include "features/time/lunar_calendar_event.h"
#include "features/time/robot_time_service.h"
#include "integration/web_chat_bridge.h"
#include "sdkconfig.h"

#include <functional>
#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
#include <atomic>
#include <esp_timer.h>
#endif
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

class KyNangSoRobotDisplay;

namespace kynangso {
class SecondaryDisplay;
class SdStorage;

// Product commands shared by HTTP and MCP. Owns policy and delegates hardware,
// storage and UI to their owners. Application remains the system state owner.
class RobotController {
public:
    RobotController(RobotSettings& settings, RobotDrive& drive, SecondaryDisplay& secondary,
                    KyNangSoRobotDisplay& display, std::function<void(int)> apply_tft_rotation,
                    SdStorage& storage);
    ~RobotController();
    RobotTimeService& TimeService() { return *time_service_; }
    BluetoothService& Bluetooth() { return bluetooth_; }
    bool HandleBluetoothButton(int action);  // -1 previous, 1 next, 0 connect, 2 close
    FaceConfig GetFaceConfig() const;
    FaceThemeService& FaceThemes() { return *face_themes_; }
    bool ConfigureFace(const FaceConfig& config);
    InfoShuttleConfig GetInfoShuttleConfig() const;
    bool ConfigureInfoShuttle(const InfoShuttleConfig& config);
    bool AdvanceInfoShuttle();
    bool RefreshInfoShuttleWeather();
    bool SetInfoShuttleWeather(std::string summary, int icon_kind = -1);
    void ShowLunarCalendarResult(int solar_day, int solar_month, int solar_year,
                                 const VietnameseLunarDate& lunar,
                                 LunarCalendarEvents events = {}, size_t event_count = 0,
                                 size_t total_event_count = 0, bool events_loaded = false);
    bool OfflineDiagnosticsEnabled() const;
    bool ConfigureOfflineDiagnostics(bool enabled);
    bool ShowNews(std::string headline, std::string source);
    bool StartNameWheel(std::vector<std::string> names, std::string winner);
    bool PreviewEmotion(int preset);
    bool PreviewFace(int emotion);
    SdStorage& Storage() { return storage_; }
    int GetVolume() const;
    bool SetVolume(int volume);

    MotorConfig GetMotorConfig() const;
    bool ConfigureMotor(const MotorConfig& config);
    bool Move(RobotDrive::Direction direction, int steps, int delay_ms = 0);
    bool Step(int steps, int delay_ms = 0);
    void Stop();
    bool StartRoutine(RobotDrive::Routine routine);
    bool IsRoutineRunning() const;
    void SetDurationUnit(int delay_ms);

    bool IsOledEnabled() const;
    bool IsOledAvailable() const;
    esp_err_t OledLastError() const;
    void ApplyOledExtension();
    OledConfig GetOledConfig() const;
    bool ConfigureOled(const OledConfig& config);
    bool SetOledMode(int mode);
    bool SetOledMarqueeSpeed(int speed);
    bool SaveOledMessage(const std::string& text, int speed, int font_size);
    void ReinitializeOled();
    std::string ShowIp();
    bool ShowOledText(std::string text, int duration_ms, int speed = 2);
    bool ShowPaymentQr(int duration_ms);
    QrConfig GetQrConfig() const;
    bool ConfigureQr(const QrConfig& config);
    bool ShowQr(std::string payload, std::string caption, int duration_ms);
    bool ShowIpQr(int duration_ms);
    void SetEmotion(std::string emotion);

    int GetTftRotation() const;
    bool SetTftRotation(int rotation);
    BootAudioConfig GetBootAudioConfig() const;
    bool SetBootAudioConfig(const BootAudioConfig& config);

    bool QueueWebPrompt(std::string prompt);
    const char* WebPromptStatus();
    std::expected<std::string, std::string> ConsumeWebPrompt();

private:
    std::atomic<bool> weather_stop_{false};
    std::atomic<bool> weather_refresh_requested_{false};
    std::thread weather_worker_;
    RobotSettings& settings_;
    RobotDrive& drive_;
    SecondaryDisplay& secondary_;
    KyNangSoRobotDisplay& display_;
    SdStorage& storage_;
    std::function<void(int)> apply_tft_rotation_;
    WebChatBridge web_chat_;
    std::shared_ptr<RobotTimeService> time_service_;
    std::shared_ptr<std::atomic<bool>> face_theme_alive_ =
        std::make_shared<std::atomic<bool>>(true);
    std::unique_ptr<FaceThemeService> face_themes_;
    BluetoothService bluetooth_;
#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
    struct BluetoothUiLifetime { std::atomic<bool> alive{true}; std::atomic<bool> pending{false}; };
    std::shared_ptr<BluetoothUiLifetime> bluetooth_ui_lifetime_ = std::make_shared<BluetoothUiLifetime>();
    esp_timer_handle_t bluetooth_ui_timer_ = nullptr;
    std::atomic<bool> bluetooth_ui_active_{false};
    uint32_t bluetooth_operation_id_ = 0;
    int64_t bluetooth_started_us_ = 0;
    int64_t bluetooth_deadline_us_ = 0;
    int64_t bluetooth_finished_us_ = 0;
    size_t bluetooth_selected_ = 0;
    std::string bluetooth_error_;
    int64_t bluetooth_error_until_us_ = 0;
    std::string bluetooth_view_key_;
    std::string bluetooth_oled_key_;
    void UpdateBluetoothUi();
    void CloseBluetoothUi();
#endif
    std::mutex config_mutex_;
};

}  // namespace kynangso

#endif  // KYNANGSO_ROBOT_CONTROLLER_H_
