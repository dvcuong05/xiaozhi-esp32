#include "robot_controller.h"

#include "application.h"
#include "mrkenddo_robot_display.h"
#include "secondary_display.h"

#include <utility>

#include <wifi_manager.h>

namespace mrkenddo {

RobotController::RobotController(RobotSettings& settings, RobotDrive& drive,
                                 SecondaryDisplay& secondary, MrkenDDoRobotDisplay& display,
                                 std::function<void(int)> apply_tft_rotation)
    : settings_(settings),
      drive_(drive),
      secondary_(secondary),
      display_(display),
      apply_tft_rotation_(std::move(apply_tft_rotation)) {
    const auto config = settings_.LoadMotor();
    drive_.SetInversion(config.left_inverted, config.right_inverted);
    drive_.SetSpeed(config.delay_ms);
}

MotorConfig RobotController::GetMotorConfig() const { return settings_.LoadMotor(); }

bool RobotController::ConfigureMotor(const MotorConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (settings_.SaveMotor(config) != ESP_OK)
        return false;
    drive_.SetInversion(config.left_inverted, config.right_inverted);
    drive_.SetSpeed(config.delay_ms);
    return true;
}

bool RobotController::Move(RobotDrive::Direction direction, int steps, int delay_ms) {
    return drive_.Move(direction, steps, delay_ms);
}
bool RobotController::Step(int steps, int delay_ms) { return drive_.Step(steps, delay_ms); }
void RobotController::Stop() { drive_.Stop(); }
bool RobotController::StartRoutine(RobotDrive::Routine routine) {
    return drive_.StartRoutine(routine);
}
bool RobotController::IsRoutineRunning() const { return drive_.IsRoutineRunning(); }
void RobotController::SetDurationUnit(int delay_ms) { drive_.SetSpeed(delay_ms); }

OledConfig RobotController::GetOledConfig() const { return settings_.LoadOled(); }

bool RobotController::ConfigureOled(const OledConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (settings_.SaveOled(config) != ESP_OK)
        return false;
    ReinitializeOled();
    return true;
}

bool RobotController::SetOledMode(int mode) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (settings_.SetOledMode(mode) != ESP_OK)
        return false;
    Application::GetInstance().Schedule([this]() { secondary_.ApplySettings(); });
    return true;
}

bool RobotController::SaveOledMessage(const std::string& text, int speed, int font_size) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (settings_.SaveMarquee(text, speed, font_size) != ESP_OK)
        return false;
    Application::GetInstance().Schedule([this]() { secondary_.ApplySettings(); });
    return true;
}

void RobotController::ReinitializeOled() {
    Application::GetInstance().Schedule([this]() { secondary_.Reinitialize(); });
}

std::string RobotController::ShowIp() {
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsConnected())
        return {};
    const auto address = wifi.GetIpAddress();
    Application::GetInstance().Schedule([this, address]() {
        secondary_.ShowIp(address);
        const std::string message = "IP: " + address;
        display_.ShowNotification(message.c_str(), 10000);
    });
    return address;
}

bool RobotController::ShowOledText(std::string text, int duration_ms) {
    if (text.empty() || text.size() > OledDisplayController::kMaxTextLength ||
        text.find('\0') != std::string::npos || duration_ms < 1000 || duration_ms > 30000 ||
        !secondary_.IsAvailable())
        return false;
    Application::GetInstance().Schedule(
        [this, text = std::move(text), duration_ms]() { secondary_.ShowText(text, duration_ms); });
    return true;
}

bool RobotController::ShowPaymentQr(int duration_ms) {
    if (duration_ms < 5000 || duration_ms > 120000)
        return false;
    const auto config = settings_.LoadQr();
    if (!config.valid)
        return false;
    if (config.payload[0] != '\0')
        return ShowQr(config.payload, config.caption, duration_ms);
    Application::GetInstance().Schedule(
        [this, duration_ms]() { display_.ShowPaymentQr(duration_ms); });
    return true;
}

QrConfig RobotController::GetQrConfig() const { return settings_.LoadQr(); }

bool RobotController::ConfigureQr(const QrConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return settings_.SaveQr(config) == ESP_OK;
}

bool RobotController::ShowQr(std::string payload, std::string caption, int duration_ms) {
    if (duration_ms < 5000 || duration_ms > 120000 || !RobotQrCode::ValidText(caption, 96))
        return false;
    auto code = std::make_shared<RobotQrCode>();
    if (!RobotQrCode::Generate(payload, *code))
        return false;
    Application::GetInstance().Schedule(
        [this, code = std::move(code), caption = std::move(caption), duration_ms]() {
            display_.ShowPaymentQr(duration_ms, code, caption);
        });
    return true;
}

bool RobotController::ShowIpQr(int duration_ms) {
    const auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsConnected())
        return false;
    const auto url = "http://" + wifi.GetIpAddress() + "/";
    return ShowQr(url, url, duration_ms);
}

void RobotController::SetEmotion(std::string emotion) {
    Application::GetInstance().Schedule(
        [this, emotion = std::move(emotion)]() { display_.SetEmotion(emotion.c_str()); });
}

int RobotController::GetTftRotation() const { return settings_.LoadTftRotation(); }

bool RobotController::SetTftRotation(int rotation) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (!apply_tft_rotation_ || settings_.SaveTftRotation(rotation) != ESP_OK)
        return false;
    Application::GetInstance().Schedule([this, rotation]() { apply_tft_rotation_(rotation); });
    return true;
}

BootAudioConfig RobotController::GetBootAudioConfig() const { return settings_.LoadBootAudio(); }

bool RobotController::SetBootAudioConfig(const BootAudioConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (settings_.SaveBootAudio(config) != ESP_OK)
        return false;
    Application::GetInstance().Schedule([config]() {
        Application::GetInstance().SetBootAudioConfig(config.enabled, config.volume);
    });
    return true;
}

bool RobotController::QueueWebPrompt(std::string prompt) {
    return web_chat_.Queue(std::move(prompt));
}
const char* RobotController::WebPromptStatus() { return web_chat_.Status(); }
std::expected<std::string, std::string> RobotController::ConsumeWebPrompt() {
    return web_chat_.Consume();
}

}  // namespace mrkenddo
