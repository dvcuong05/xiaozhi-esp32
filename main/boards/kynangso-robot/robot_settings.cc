#include "robot_settings.h"

#include "config.h"
#include "robot_drive.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_log.h>
#include <nvs.h>
#include <soc/soc_caps.h>

namespace kynangso {
namespace {
constexpr char kTag[] = "RobotSettings";
constexpr int32_t kRotationVersion = 4;
constexpr int32_t kBootVersion = 2;
constexpr int32_t kBootVolume = 45;

class NvsHandle {
public:
    NvsHandle(const char* name, nvs_open_mode_t mode) {
        error = nvs_open(name, mode, &value);
        if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(kTag, "Cannot open %s: %s", name, esp_err_to_name(error));
        }
    }
    ~NvsHandle() {
        if (error == ESP_OK) {
            nvs_close(value);
        }
    }
    nvs_handle_t value = 0;
    esp_err_t error = ESP_FAIL;
};

template <typename Writer>
esp_err_t WriteNamespace(const char* name, Writer write) {
    NvsHandle handle(name, NVS_READWRITE);
    if (handle.error != ESP_OK) {
        return handle.error;
    }
    esp_err_t error = write(handle.value);
    if (error == ESP_OK) {
        error = nvs_commit(handle.value);
    }
    return error;
}

bool ReadBool(nvs_handle_t handle, const char* key, bool fallback) {
    uint8_t value = 0;
    return nvs_get_u8(handle, key, &value) == ESP_OK && value <= 1 ? value != 0 : fallback;
}
}  // namespace

bool RobotSettings::ValidateMotor(const MotorConfig& config) {
    return config.steps >= 1 && config.steps <= RobotDrive::kMaxSteps &&
           config.delay_ms >= RobotDrive::kMinDelayMs && config.delay_ms <= RobotDrive::kMaxDelayMs;
}

MotorConfig RobotSettings::LoadMotor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    MotorConfig config;
    NvsHandle handle("motor_config", NVS_READONLY);
    if (handle.error == ESP_OK) {
        nvs_get_i32(handle.value, "steps", &config.steps);
        nvs_get_i32(handle.value, "delay_ms", &config.delay_ms);
        config.left_inverted = ReadBool(handle.value, "left_inv", false);
        config.right_inverted = ReadBool(handle.value, "right_inv", true);
    }
    config.steps = std::clamp<int32_t>(config.steps, 1, RobotDrive::kMaxSteps);
    config.delay_ms =
        std::clamp<int32_t>(config.delay_ms, RobotDrive::kMinDelayMs, RobotDrive::kMaxDelayMs);
    return config;
}

esp_err_t RobotSettings::SaveMotor(const MotorConfig& config) {
    if (!ValidateMotor(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return WriteNamespace("motor_config", [&config](nvs_handle_t handle) {
        esp_err_t error = nvs_set_i32(handle, "steps", config.steps);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "delay_ms", config.delay_ms);
        if (error == ESP_OK)
            error = nvs_set_u8(handle, "left_inv", config.left_inverted);
        if (error == ESP_OK)
            error = nvs_set_u8(handle, "right_inv", config.right_inverted);
        return error;
    });
}

OledConfig RobotSettings::DefaultOled() {
    OledConfig config;
    std::snprintf(config.address, sizeof(config.address), "%02X", SECOND_DISPLAY_I2C_ADDRESS);
    config.sda = SECOND_DISPLAY_SDA_PIN;
    config.scl = SECOND_DISPLAY_SCL_PIN;
    config.width = SECOND_DISPLAY_WIDTH;
    config.height = SECOND_DISPLAY_HEIGHT;
    return config;
}

bool RobotSettings::ValidOledPins(int sda, int scl) {
    return sda >= 0 && scl >= 0 && sda < SOC_GPIO_PIN_COUNT && scl < SOC_GPIO_PIN_COUNT &&
           sda != scl;
}

bool RobotSettings::ValidateOled(const OledConfig& config) {
    if (config.address[2] != '\0' ||
        std::memchr(config.text, '\0', sizeof(config.text)) == nullptr) {
        return false;
    }
    char* end = nullptr;
    const long address = std::strtol(config.address, &end, 16);
    return end != config.address && *end == '\0' && address >= 0x03 && address <= 0x77 &&
           ValidOledPins(config.sda, config.scl) && config.width >= 64 && config.width <= 256 &&
           config.width % 8 == 0 && config.height >= 16 && config.height <= 64 &&
           config.height % 8 == 0 && (config.rotation == 0 || config.rotation == 180) &&
           OledDisplayController::IsValidMode(config.mode) &&
           OledDisplayController::IsValidSpeed(config.speed) &&
           OledDisplayController::IsValidFontSize(config.font_size) && config.contrast >= 0 &&
           config.contrast <= 255;
}

OledConfig RobotSettings::LoadOled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    OledConfig config = DefaultOled();
    NvsHandle handle("oled_config", NVS_READONLY);
    if (handle.error == ESP_OK) {
        size_t length = sizeof(config.address);
        if (nvs_get_str(handle.value, "addr", config.address, &length) != ESP_OK) {
            std::snprintf(config.address, sizeof(config.address), "%02X",
                          SECOND_DISPLAY_I2C_ADDRESS);
        }
        nvs_get_i32(handle.value, "sda", &config.sda);
        nvs_get_i32(handle.value, "scl", &config.scl);
        nvs_get_i32(handle.value, "width", &config.width);
        nvs_get_i32(handle.value, "height", &config.height);
        nvs_get_i32(handle.value, "rotation", &config.rotation);
        nvs_get_i32(handle.value, "mode", &config.mode);
        nvs_get_i32(handle.value, "speed", &config.speed);
        nvs_get_i32(handle.value, "font_size", &config.font_size);
        nvs_get_i32(handle.value, "contrast", &config.contrast);
        length = sizeof(config.text);
        if (nvs_get_str(handle.value, "text", config.text, &length) != ESP_OK) {
            config.text[0] = '\0';
        }
        config.invert = ReadBool(handle.value, "invert", false);
    }
    // A damaged optional preference must not discard valid custom wiring.
    // Match the field-by-field fallbacks used before the settings extraction.
    if (config.rotation != 0 && config.rotation != 180)
        config.rotation = 0;
    if (!OledDisplayController::IsValidMode(config.mode)) {
        config.mode = static_cast<int32_t>(OledDisplayMode::Clock);
    }
    if (!OledDisplayController::IsValidSpeed(config.speed))
        config.speed = 2;
    if (!OledDisplayController::IsValidFontSize(config.font_size))
        config.font_size = 0;
    if (config.contrast < 0 || config.contrast > 255)
        config.contrast = 255;
    if (!ValidateOled(config)) {
        ESP_LOGW(kTag, "Invalid saved OLED configuration; using hardware defaults");
        return DefaultOled();
    }
    return config;
}

esp_err_t RobotSettings::SaveOled(const OledConfig& config) {
    if (!ValidateOled(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return WriteNamespace("oled_config", [&config](nvs_handle_t handle) {
        esp_err_t error = nvs_set_str(handle, "addr", config.address);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "sda", config.sda);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "scl", config.scl);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "width", config.width);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "height", config.height);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "rotation", config.rotation);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "mode", config.mode);
        if (error == ESP_OK)
            error = nvs_set_str(handle, "text", config.text);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "speed", config.speed);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "font_size", config.font_size);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "contrast", config.contrast);
        if (error == ESP_OK)
            error = nvs_set_u8(handle, "invert", config.invert);
        return error;
    });
}

esp_err_t RobotSettings::SetOledMode(int mode) {
    if (!OledDisplayController::IsValidMode(mode)) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return WriteNamespace(
        "oled_config", [mode](nvs_handle_t handle) { return nvs_set_i32(handle, "mode", mode); });
}

esp_err_t RobotSettings::SaveMarquee(const std::string& text, int speed, int font_size) {
    if (text.empty() || text.size() > OledDisplayController::kMaxTextLength ||
        text.find('\0') != std::string::npos || !OledDisplayController::IsValidSpeed(speed) ||
        !OledDisplayController::IsValidFontSize(font_size)) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return WriteNamespace("oled_config", [&](nvs_handle_t handle) {
        esp_err_t error =
            nvs_set_i32(handle, "mode", static_cast<int32_t>(OledDisplayMode::Marquee));
        if (error == ESP_OK)
            error = nvs_set_str(handle, "text", text.c_str());
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "speed", speed);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "font_size", font_size);
        return error;
    });
}

bool RobotSettings::IsValidTftRotation(int rotation) {
    return rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270;
}

int RobotSettings::LoadTftRotation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int32_t rotation = DISPLAY_DEFAULT_ROTATION;
    int32_t version = 0;
    bool valid = false;
    {
        NvsHandle handle("tft_config", NVS_READONLY);
        if (handle.error == ESP_OK) {
            valid = nvs_get_i32(handle.value, "rotation", &rotation) == ESP_OK &&
                    IsValidTftRotation(rotation);
            nvs_get_i32(handle.value, "rotation_v", &version);
        }
    }
    if (!valid)
        rotation = DISPLAY_DEFAULT_ROTATION;
    if (version < kRotationVersion) {
        // Preserve the existing one-time correction for the old 270-degree default.
        if (valid && rotation == 270)
            rotation = DISPLAY_DEFAULT_ROTATION;
        const esp_err_t error = WriteNamespace("tft_config", [rotation](nvs_handle_t handle) {
            esp_err_t result = nvs_set_i32(handle, "rotation", rotation);
            if (result == ESP_OK)
                result = nvs_set_i32(handle, "rotation_v", kRotationVersion);
            return result;
        });
        if (error != ESP_OK)
            ESP_LOGW(kTag, "TFT migration failed: %s", esp_err_to_name(error));
    }
    return rotation;
}

esp_err_t RobotSettings::SaveTftRotation(int rotation) {
    if (!IsValidTftRotation(rotation))
        return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(mutex_);
    return WriteNamespace("tft_config", [rotation](nvs_handle_t handle) {
        esp_err_t error = nvs_set_i32(handle, "rotation", rotation);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "rotation_v", kRotationVersion);
        return error;
    });
}

BootAudioConfig RobotSettings::LoadBootAudio() const {
    std::lock_guard<std::mutex> lock(mutex_);
    BootAudioConfig config{true, kBootVolume};
    int32_t version = 0;
    int32_t volume = kBootVolume;
    {
        NvsHandle handle("boot_sequence", NVS_READONLY);
        if (handle.error == ESP_OK) {
            config.enabled = ReadBool(handle.value, "enabled", true);
            nvs_get_i32(handle.value, "volume", &volume);
            nvs_get_i32(handle.value, "version", &version);
        }
    }
    if (version == 0 && volume == 22) {
        volume = kBootVolume;
        const esp_err_t error = WriteNamespace("boot_sequence", [](nvs_handle_t handle) {
            esp_err_t result = nvs_set_i32(handle, "volume", kBootVolume);
            if (result == ESP_OK)
                result = nvs_set_i32(handle, "version", kBootVersion);
            return result;
        });
        if (error != ESP_OK)
            ESP_LOGW(kTag, "Boot audio migration failed: %s", esp_err_to_name(error));
    }
    config.volume = std::clamp<int32_t>(volume, 0, 100);
    return config;
}

esp_err_t RobotSettings::SaveBootAudio(const BootAudioConfig& config) {
    if (config.volume < 0 || config.volume > 100)
        return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(mutex_);
    return WriteNamespace("boot_sequence", [&config](nvs_handle_t handle) {
        esp_err_t error = nvs_set_u8(handle, "enabled", config.enabled);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "volume", config.volume);
        if (error == ESP_OK)
            error = nvs_set_i32(handle, "version", kBootVersion);
        return error;
    });
}

QrConfig RobotSettings::LoadQr() const {
    std::lock_guard<std::mutex> lock(mutex_);
    QrConfig config;
    // One versioned record avoids a new recipient payload with an old caption
    // after a failed/partial multi-key update.
    std::array<uint8_t, 1 + sizeof(config.payload) + sizeof(config.caption)> record{};
    size_t size = record.size();
    NvsHandle handle("qr_config", NVS_READONLY);
    if (handle.error == ESP_ERR_NVS_NOT_FOUND)
        return config;
    const auto error = handle.error == ESP_OK
                           ? nvs_get_blob(handle.value, "record", record.data(), &size)
                           : handle.error;
    if (error == ESP_ERR_NVS_NOT_FOUND)
        return config;
    if (error != ESP_OK || size != record.size() || record[0] != 1) {
        config.valid = false;
        return config;
    }
    std::memcpy(config.payload, record.data() + 1, sizeof(config.payload));
    std::memcpy(config.caption, record.data() + 1 + sizeof(config.payload), sizeof(config.caption));
    if (!RobotQrCode::ValidateConfig(config)) {
        config = {};
        config.valid = false;
    }
    return config;
}

esp_err_t RobotSettings::SaveQr(const QrConfig& config) {
    if (!RobotQrCode::ValidateConfig(config))
        return ESP_ERR_INVALID_ARG;
    std::array<uint8_t, 1 + sizeof(config.payload) + sizeof(config.caption)> record{};
    record[0] = 1;
    std::memcpy(record.data() + 1, config.payload, sizeof(config.payload));
    std::memcpy(record.data() + 1 + sizeof(config.payload), config.caption, sizeof(config.caption));
    std::lock_guard<std::mutex> lock(mutex_);
    return WriteNamespace("qr_config", [&record](nvs_handle_t handle) {
        return nvs_set_blob(handle, "record", record.data(), record.size());
    });
}

FaceConfig RobotSettings::LoadFace() const {
    std::lock_guard<std::mutex> lock(mutex_);
    FaceConfig config;
    uint8_t record[7] = {};
    size_t length = sizeof(record);
    NvsHandle handle("face_config", NVS_READONLY);
    if (handle.error == ESP_OK && nvs_get_blob(handle.value, "record", record, &length) == ESP_OK &&
        length == sizeof(record) && record[0] == 1 && record[1] <= 1 && record[2] <= 1 &&
        record[3] <= 1 && record[4] <= 1 && record[5] >= 20 && record[5] <= 100 &&
        record[6] < kFaceEmotionCount) {
        config = {record[1] != 0, record[2] != 0, record[3] != 0,
                  record[4] != 0, record[5],      record[6]};
    }
    return config;
}

esp_err_t RobotSettings::SaveFace(const FaceConfig& config) {
    if (config.intensity < 20 || config.intensity > 100 || config.idle_emotion < 0 ||
        config.idle_emotion >= kFaceEmotionCount)
        return ESP_ERR_INVALID_ARG;
    const uint8_t record[] = {1,
                              config.eyes,
                              config.animation,
                              config.follow_emotion,
                              config.motion,
                              static_cast<uint8_t>(config.intensity),
                              static_cast<uint8_t>(config.idle_emotion)};
    std::lock_guard<std::mutex> lock(mutex_);
    return WriteNamespace("face_config", [&record](nvs_handle_t handle) {
        return nvs_set_blob(handle, "record", record, sizeof(record));
    });
}

}  // namespace kynangso
