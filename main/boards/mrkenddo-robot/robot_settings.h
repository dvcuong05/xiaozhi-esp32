#ifndef MRKENDDO_ROBOT_SETTINGS_H_
#define MRKENDDO_ROBOT_SETTINGS_H_

#include "boot_sequence.h"
#include "oled_display_controller.h"
#include "robot_qr_code.h"

#include <cstdint>
#include <mutex>
#include <string>

#include <esp_err.h>

namespace mrkenddo {

// Append only: the index is persisted in the existing version-1 NVS record.
inline constexpr const char* kFaceEmotions[] = {
    "neutral", "happy", "sad",   "angry",      "surprised", "sleepy",    "confused", "listening",
    "laugh",   "love",  "dizzy", "distracted", "devil",     "confident", "yawn",     "playful"};
inline constexpr int kFaceEmotionCount = sizeof(kFaceEmotions) / sizeof(kFaceEmotions[0]);

struct MotorConfig {
    int32_t steps = 512;
    int32_t delay_ms = 1;
    bool left_inverted = false;
    bool right_inverted = true;
};

struct FaceConfig {
    bool eyes = true;
    bool animation = true;
    bool follow_emotion = true;
    bool motion = false;
    int intensity = 60;
    int idle_emotion = 0;
};

struct OledConfig {
    char address[3] = {};
    int32_t sda = 0;
    int32_t scl = 0;
    int32_t width = 128;
    int32_t height = 32;
    int32_t rotation = 0;
    int32_t mode = static_cast<int32_t>(OledDisplayMode::Clock);
    char text[sizeof(OledDisplaySettings::text)] = {};
    int32_t speed = 2;
    int32_t font_size = 0;
    int32_t contrast = 255;
    bool invert = false;
};

// Owns the persistent schema, defaults and migrations for this product. Adapters
// and hardware drivers must not read/write these namespaces independently.
class RobotSettings {
public:
    MotorConfig LoadMotor() const;
    esp_err_t SaveMotor(const MotorConfig& config);
    OledConfig LoadOled() const;
    esp_err_t SaveOled(const OledConfig& config);
    esp_err_t SetOledMode(int mode);
    esp_err_t SaveMarquee(const std::string& text, int speed, int font_size);
    int LoadTftRotation() const;
    esp_err_t SaveTftRotation(int rotation);
    BootAudioConfig LoadBootAudio() const;
    esp_err_t SaveBootAudio(const BootAudioConfig& config);
    QrConfig LoadQr() const;
    esp_err_t SaveQr(const QrConfig& config);
    FaceConfig LoadFace() const;
    esp_err_t SaveFace(const FaceConfig& config);

    static OledConfig DefaultOled();
    static bool ValidateMotor(const MotorConfig& config);
    static bool ValidateOled(const OledConfig& config);
    static bool ValidOledPins(int sda, int scl);
    static bool IsValidTftRotation(int rotation);

private:
    mutable std::mutex mutex_;
};

}  // namespace mrkenddo

#endif  // MRKENDDO_ROBOT_SETTINGS_H_
