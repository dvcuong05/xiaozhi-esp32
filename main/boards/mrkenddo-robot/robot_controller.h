#ifndef MRKENDDO_ROBOT_CONTROLLER_H_
#define MRKENDDO_ROBOT_CONTROLLER_H_

#include "robot_drive.h"
#include "robot_settings.h"
#include "web_chat_bridge.h"

#include <functional>
#include <mutex>
#include <string>

class MrkenDDoRobotDisplay;

namespace mrkenddo {
class SecondaryDisplay;

// Product commands shared by HTTP and MCP. Owns policy and delegates hardware,
// storage and UI to their owners. Application remains the system state owner.
class RobotController {
public:
    RobotController(RobotSettings& settings, RobotDrive& drive, SecondaryDisplay& secondary,
                    MrkenDDoRobotDisplay& display, std::function<void(int)> apply_tft_rotation);

    MotorConfig GetMotorConfig() const;
    bool ConfigureMotor(const MotorConfig& config);
    bool Move(RobotDrive::Direction direction, int steps, int delay_ms = 0);
    bool Step(int steps, int delay_ms = 0);
    void Stop();
    bool StartRoutine(RobotDrive::Routine routine);
    bool IsRoutineRunning() const;
    void SetDurationUnit(int delay_ms);

    OledConfig GetOledConfig() const;
    bool ConfigureOled(const OledConfig& config);
    bool SetOledMode(int mode);
    bool SaveOledMessage(const std::string& text, int speed, int font_size);
    void ReinitializeOled();
    std::string ShowIp();
    bool ShowOledText(std::string text, int duration_ms);
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
    RobotSettings& settings_;
    RobotDrive& drive_;
    SecondaryDisplay& secondary_;
    MrkenDDoRobotDisplay& display_;
    std::function<void(int)> apply_tft_rotation_;
    WebChatBridge web_chat_;
    std::mutex config_mutex_;
};

}  // namespace mrkenddo

#endif  // MRKENDDO_ROBOT_CONTROLLER_H_
