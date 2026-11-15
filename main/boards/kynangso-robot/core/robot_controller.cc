#include "core/robot_controller.h"

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "display/oled/secondary_display.h"
#include "display/tft/kynangso_robot_display.h"
#include "features/storage/sd_storage.h"

#include <esp_random.h>
#include <cJSON.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>
#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
#include <freertos/FreeRTOS.h>
#endif

#include <wifi_manager.h>

namespace kynangso {

RobotController::RobotController(RobotSettings& settings, RobotDrive& drive,
                                 SecondaryDisplay& secondary, KyNangSoRobotDisplay& display,
                                 std::function<void(int)> apply_tft_rotation, SdStorage& storage)
    : settings_(settings),
      drive_(drive),
      secondary_(secondary),
      display_(display),
      storage_(storage),
      apply_tft_rotation_(std::move(apply_tft_rotation)) {
    time_service_ =
        std::make_shared<RobotTimeService>([this](const TimeView& view, TimeRingtone ringtone) {
            display_.SetTimeView(view);
            if (ringtone == TimeRingtone::None)
                return;

            static constexpr const char* kPaths[] = {
                nullptr,
                "/robot/ringtones/alarm.mp3",
                "/robot/ringtones/reminder.mp3",
                "/robot/ringtones/countdown.mp3",
                "/robot/ringtones/pomodoro_focus.mp3",
                "/robot/ringtones/pomodoro_break.mp3",
                "/robot/ringtones/pomodoro_complete.mp3",
            };
            const auto index = static_cast<size_t>(ringtone);
            if (index >= sizeof(kPaths) / sizeof(kPaths[0]) || kPaths[index] == nullptr)
                return;

            NotifyPlayer::InputFactory input;
            if (auto result = storage_.CreateAudioInput(kPaths[index], input); !result) {
                // The SD card is optional. Keep clock alerts audible with the
                // built-in sound when a custom ringtone cannot be opened.
                Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
                return;
            }
            MediaPlaybackMetadata metadata;
            metadata.title = kPaths[index] + std::string("/robot/ringtones/").size();
            metadata.source_path = kPaths[index];
            Application::GetInstance().RequestLocalMedia(std::move(metadata), std::move(input));
        });
    time_service_->Start();
    std::weak_ptr<RobotTimeService> schedule_service = time_service_;
    display_.SetUpcomingScheduleProvider([schedule_service]() -> UpcomingScheduleList {
        if (const auto service = schedule_service.lock())
            return service->UpcomingSchedules();
        return {};
    });
    display_.ConfigureInfoShuttle(settings_.LoadInfoShuttle());
    weather_worker_ = std::thread([this]() {
        std::string previous_location;
        auto next = std::chrono::steady_clock::now();
        unsigned weather_ticks = 0;
        while (!weather_stop_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (++weather_ticks % 10 != 0)
                continue;
            auto config = settings_.LoadInfoShuttle();
            if (!config.enabled || !config.show_weather)
                continue;
            auto now = std::chrono::steady_clock::now();
            if (config.weather_location != previous_location) {
                previous_location = config.weather_location;
                next = now;
                display_.SetInfoShuttleWeather("");
            }
            if (now < next || !Application::GetInstance().IsDeviceBridgeConnected())
                continue;
            next = now + std::chrono::seconds(120 + esp_random() % 31);
            cJSON* payload = cJSON_CreateObject();
            if (!payload)
                continue;
            cJSON_AddStringToObject(payload, "op", "weather.current");
            cJSON_AddStringToObject(payload, "location", config.weather_location.c_str());
            char* encoded = cJSON_PrintUnformatted(payload);
            cJSON_Delete(payload);
            if (!encoded)
                continue;
            std::string request(encoded), result, error;
            cJSON_free(encoded);
            if (!Application::GetInstance().RequestService(request, &result, &error))
                continue;
            cJSON* response = cJSON_Parse(result.c_str());
            const auto* summary = cJSON_GetObjectItemCaseSensitive(response, "summary");
            const auto* icon = cJSON_GetObjectItemCaseSensitive(response, "icon_kind");
            // Discard responses for a location changed while the RPC was in flight.
            if (!weather_stop_.load() && cJSON_IsString(summary) && summary->valuestring &&
                std::strlen(summary->valuestring) <= 192 &&
                settings_.LoadInfoShuttle().weather_location == config.weather_location) {
                display_.SetInfoShuttleWeather(summary->valuestring,
                                               cJSON_IsNumber(icon) ? icon->valueint : -1);
                next = std::chrono::steady_clock::now() +
                       std::chrono::seconds(900 + esp_random() % 61);
            }
            cJSON_Delete(response);
        }
    });

#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
    if (bluetooth_.GetStatus().enabled) {
        esp_timer_create_args_t args = {};
        args.name = "bluetooth_ui";
        args.arg = this;
        args.callback = [](void* arg) {
            auto* self = static_cast<RobotController*>(arg);
            auto life = self->bluetooth_ui_lifetime_;
            if (!life->alive.load() || life->pending.exchange(true))
                return;
            Application::GetInstance().Schedule([self, life]() {
                if (life->alive.load())
                    self->UpdateBluetoothUi();
                life->pending.store(false);
            });
        };
        if (esp_timer_create(&args, &bluetooth_ui_timer_) == ESP_OK) {
            if (esp_timer_start_periodic(bluetooth_ui_timer_, 500000) != ESP_OK) {
                esp_timer_delete(bluetooth_ui_timer_);
                bluetooth_ui_timer_ = nullptr;
            }
        }
    }
#endif
    const auto config = settings_.LoadMotor();
    drive_.SetInversion(config.left_inverted, config.right_inverted);
    drive_.SetSpeed(config.delay_ms);
    const auto face = settings_.LoadFace();
    display_.ConfigureFace(face);
    drive_.ConfigureEmotionMotion(face.motion, face.intensity);
    display_.SetBehaviorCallbacks(
        [this](bool speaking) { drive_.SetSpeaking(speaking); },
        [this](const char* emotion) {
            const std::string name(emotion);
            int preset = 0;
            if (name == "laugh" || name == "joy")
                preset = 1;
            else if (name == "angry" || name == "anger")
                preset = 2;
            else if (name == "surprised" || name == "astonished" || name == "wow" ||
                     name == "panic")
                preset = 3;
            else if (name == "sad" || name == "cry" || name == "disappointed")
                preset = 4;
            else if (name == "thinking" || name == "confused")
                preset = 5;
            else if (name == "sleepy" || name == "sleep")
                preset = 6;
            else if (name == "confident" || name == "cool")
                preset = 7;
            drive_.PlayEmotion(preset);
        });
}

RobotController::~RobotController() {
    weather_stop_.store(true);
    if (weather_worker_.joinable())
        weather_worker_.join();
#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
    bluetooth_ui_lifetime_->alive.store(false);
    if (bluetooth_ui_timer_) {
        esp_timer_stop_blocking(bluetooth_ui_timer_, portMAX_DELAY);
        esp_timer_delete(bluetooth_ui_timer_);
    }
    CloseBluetoothUi();
#endif
    time_service_->Stop();
    display_.SetUpcomingScheduleProvider({});
    display_.SetBehaviorCallbacks({}, {});
    drive_.ConfigureEmotionMotion(false, 60);
}

FaceConfig RobotController::GetFaceConfig() const { return settings_.LoadFace(); }

int RobotController::GetVolume() const {
    return Board::GetInstance().GetAudioCodec()->output_volume();
}

bool RobotController::SetVolume(int volume) {
    if (volume < 0 || volume > 100)
        return false;
    Application::GetInstance().Schedule(
        [volume]() { Board::GetInstance().GetAudioCodec()->SetOutputVolume(volume); });
    return true;
}

bool RobotController::ConfigureFace(const FaceConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (settings_.SaveFace(config) != ESP_OK)
        return false;
    drive_.ConfigureEmotionMotion(config.motion, config.intensity);
    Application::GetInstance().Schedule([this, config]() { display_.ConfigureFace(config); });
    return true;
}

InfoShuttleConfig RobotController::GetInfoShuttleConfig() const {
    return settings_.LoadInfoShuttle();
}

bool RobotController::ConfigureInfoShuttle(const InfoShuttleConfig& config) {
    if (!RobotSettings::ValidateInfoShuttle(config))
        return false;
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (settings_.SaveInfoShuttle(config) != ESP_OK)
        return false;
    Application::GetInstance().Schedule([this, config]() { display_.ConfigureInfoShuttle(config); });
    return true;
}

bool RobotController::AdvanceInfoShuttle() {
    if (!settings_.LoadInfoShuttle().enabled)
        return false;
    Application::GetInstance().Schedule([this]() { display_.ShowNextInfoShuttleCard(); });
    return true;
}

bool RobotController::SetInfoShuttleWeather(std::string summary, int icon_kind) {
    if (summary.size() > 192 ||
        std::any_of(summary.begin(), summary.end(), [](unsigned char value) {
            return value < 0x20 && value != '\n' && value != '\t';
        }))
        return false;
    if (icon_kind < -1 || icon_kind > 6)
        return false;
    Application::GetInstance().Schedule([this, summary = std::move(summary), icon_kind]() {
        display_.SetInfoShuttleWeather(summary, icon_kind);
    });
    return true;
}

bool RobotController::PreviewFace(int emotion) {
    if (emotion < 0 || emotion >= kFaceEmotionCount)
        return false;
    Application::GetInstance().Schedule(
        [this, emotion]() { display_.PreviewFaceEmotion(kFaceEmotions[emotion], 5000); });
    return true;
}

bool RobotController::PreviewEmotion(int preset) {
    if (!drive_.PlayEmotion(preset, true))
        return false;
    static constexpr const char* emotions[] = {"happy", "laugh",    "angry",  "surprised",
                                               "sad",   "confused", "sleepy", "confident"};
    const std::string emotion = emotions[preset];
    Application::GetInstance().Schedule(
        [this, emotion]() { display_.PreviewFaceEmotion(emotion.c_str()); });
    return true;
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

bool RobotController::IsOledEnabled() const { return secondary_.IsEnabled(); }
bool RobotController::IsOledAvailable() const { return secondary_.IsAvailable(); }
esp_err_t RobotController::OledLastError() const { return secondary_.LastError(); }
void RobotController::ApplyOledExtension() {
    Application::GetInstance().Schedule(
        [this]() { secondary_.SetEnabled(settings_.LoadExtensions().oled); });
}

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

#if CONFIG_KYNANGSO_ROBOT_D1_BLUETOOTH_SUPPORT
void RobotController::CloseBluetoothUi() {
    bluetooth_ui_active_.store(false);
    display_.HideBluetooth();
    secondary_.SetBluetoothStatus("", 0);
    bluetooth_view_key_.clear();
    bluetooth_oled_key_.clear();
}

void RobotController::UpdateBluetoothUi() {
    const auto status = bluetooth_.GetStatus();
    const int64_t now = esp_timer_get_time();
    if (status.operation_id != bluetooth_operation_id_) {
        bluetooth_operation_id_ = status.operation_id;
        bluetooth_started_us_ = now;
        bluetooth_deadline_us_ = now + 120000000;
        bluetooth_finished_us_ = 0;
        bluetooth_error_.clear();
        bluetooth_error_until_us_ = 0;
        bluetooth_selected_ = 0;
        if (status.operation == 2) {
            for (size_t i = 0; i < status.devices.size(); ++i)
                if (status.devices[i].address == status.target_address) {
                    bluetooth_selected_ = i;
                    break;
                }
        }
        bluetooth_view_key_.clear();
        bluetooth_oled_key_.clear();
        bluetooth_ui_active_.store(true);
    }
    if (!bluetooth_ui_active_.load())
        return;
    if (now >= bluetooth_deadline_us_) {
        CloseBluetoothUi();
        return;
    }
    bluetooth_selected_ =
        status.devices.empty() ? 0 : std::min(bluetooth_selected_, status.devices.size() - 1);
    const bool grace = now - bluetooth_started_us_ < 1500000;
    const bool timed_out = now - bluetooth_started_us_ >= 60000000;
    const bool busy = !timed_out && (grace || status.scanning || status.switching ||
                                     (status.operation == 3 && status.connected));
    std::string message;
    int phase = 0;
    if (timed_out &&
        (status.scanning || status.switching || (status.operation == 3 && status.connected))) {
        message = "Thao tác quá thời gian chờ. Hãy thử lại.";
        phase = 3;
    } else if (!status.online) {
        message = "Mất liên lạc với D1 Mini";
        phase = 3;
    } else if (status.operation == 3 && busy) {
        message = "Đang ngắt kết nối loa...";
        phase = 1;
    } else if (status.scanning || (status.operation == 1 && grace)) {
        message = "Đang quét thiết bị...";
        phase = 0;
    } else if (status.switching || (status.operation == 2 && grace)) {
        message = "Đang kết nối: " + status.target;
        phase = 1;
    } else if (status.operation == 1) {
        message = "Tìm thấy " + std::to_string(status.devices.size()) + " loa Bluetooth";
        phase = status.devices.empty() ? 3 : 2;
    } else if (status.operation == 3) {
        message = "Đã ngắt kết nối loa";
        phase = 2;
    } else if (status.connected && (status.target.empty() || status.current == status.target)) {
        message = "Đã kết nối: " + status.current;
        phase = 2;
    } else {
        message = "Không kết nối được loa. Hãy thử lại.";
        phase = 3;
    }
    if ((!busy || !status.online) && !bluetooth_finished_us_) {
        bluetooth_finished_us_ = now;
        if (status.operation != 1)
            bluetooth_deadline_us_ = now + 5000000;
    }
    // OLED feedback is a transient layer; saved mode/text are never changed.
    const bool error_visible = now < bluetooth_error_until_us_;
    if (error_visible) {
        message = bluetooth_error_;
        phase = 3;
    }
    const bool oled_visible =
        error_visible || !bluetooth_finished_us_ || now - bluetooth_finished_us_ < 3500000;
    const std::string oled_key = oled_visible ? message : "";
    if (oled_key != bluetooth_oled_key_) {
        secondary_.SetBluetoothStatus(oled_key, phase);
        bluetooth_oled_key_ = oled_key;
    }
    std::string key = message + "|" + std::to_string(bluetooth_selected_);
    for (const auto& device : status.devices)
        key += "|" + device.address + device.name + std::to_string(device.rssi);
    if (key != bluetooth_view_key_) {
        auto view_status = status;
        if (timed_out) {
            view_status.scanning = false;
            view_status.switching = false;
        }
        display_.ShowBluetooth(view_status, bluetooth_selected_, message);
        bluetooth_view_key_ = std::move(key);
    }
}

bool RobotController::HandleBluetoothButton(int action) {
    if (!bluetooth_ui_active_.load() || !display_.IsBluetoothVisible())
        return false;
    auto life = bluetooth_ui_lifetime_;
    Application::GetInstance().Schedule([this, life, action]() {
        if (!life->alive.load() || !bluetooth_ui_active_.load() || !display_.IsBluetoothVisible())
            return;
        if (action == 2) {
            CloseBluetoothUi();
            return;
        }
        const auto status = bluetooth_.GetStatus();
        if (status.scanning || status.switching)
            return;
        if (status.devices.empty()) {
            if (action == 0) {
                std::string error;
                if (!bluetooth_.Scan(&error)) {
                    bluetooth_error_ = error;
                    bluetooth_error_until_us_ = esp_timer_get_time() + 3500000;
                }
                bluetooth_view_key_.clear();
                UpdateBluetoothUi();
            }
            return;
        }
        bluetooth_selected_ = std::min(bluetooth_selected_, status.devices.size() - 1);
        if (action == 0) {
            std::string error;
            if (!bluetooth_.ConnectByAddress(status.devices[bluetooth_selected_].address, &error)) {
                bluetooth_error_ = error;
                bluetooth_error_until_us_ = esp_timer_get_time() + 3500000;
            }
        } else if (action > 0)
            bluetooth_selected_ = (bluetooth_selected_ + 1) % status.devices.size();
        else
            bluetooth_selected_ =
                (bluetooth_selected_ + status.devices.size() - 1) % status.devices.size();
        bluetooth_deadline_us_ = esp_timer_get_time() + 120000000;
        bluetooth_view_key_.clear();
        UpdateBluetoothUi();
    });
    return true;
}
#else
bool RobotController::HandleBluetoothButton(int action) {
    (void)action;
    return false;
}
#endif

}  // namespace kynangso
