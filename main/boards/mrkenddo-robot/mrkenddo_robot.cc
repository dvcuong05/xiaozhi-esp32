#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "led/single_led.h"
#include "mrkenddo_robot_display.h"
#include "robot_controller.h"
#include "robot_drive.h"
#include "robot_mcp_tools.h"
#include "robot_settings.h"
#include "sd_storage.h"
#include "secondary_display.h"
#include "web_server.h"
#include "wifi_board.h"

#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <cJSON.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define TAG "MRKENDDO_ROBOT"

namespace {
using SdMusicTrack = mrkenddo::SdStorage::MusicTrack;

class SdMusicPlaylistSession : public std::enable_shared_from_this<SdMusicPlaylistSession> {
public:
    SdMusicPlaylistSession(mrkenddo::SdStorage& storage, Application& application,
                           std::vector<SdMusicTrack> tracks)
        : storage_(storage), application_(application), tracks_(std::move(tracks)) {}

    mrkenddo::SdStorage::Result Start() {
        uint32_t generation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (tracks_.empty())
                return {mrkenddo::SdStorage::Code::NotFound, "Danh sách phát đang trống."};
            active_ = true;
            current_index_ = 0;
            generation = ++generation_;
        }
        return QueueTrack(generation, 0);
    }

    size_t TrackCount() const { return tracks_.size(); }

    void Cancel() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        ++generation_;
    }

    bool SkipNext(const std::string& current_path) {
        size_t next_index;
        uint32_t generation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_ || current_index_ + 1 >= tracks_.size() ||
                tracks_[current_index_].path != current_path) {
                return false;
            }
            next_index = ++current_index_;
            generation = ++generation_;
        }

        if (auto result = QueueTrack(generation, next_index); !result) {
            ESP_LOGE(TAG, "Could not skip to SD track: %s", result.message.c_str());
            return false;
        }
        return true;
    }

private:
    mrkenddo::SdStorage& storage_;
    Application& application_;
    std::vector<SdMusicTrack> tracks_;
    std::mutex mutex_;
    size_t current_index_ = 0;
    uint32_t generation_ = 0;
    bool active_ = false;

    mrkenddo::SdStorage::Result QueueTrack(uint32_t generation, size_t index) {
        SdMusicTrack track;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_ || generation_ != generation || current_index_ != index ||
                index >= tracks_.size()) {
                return {mrkenddo::SdStorage::Code::Busy, "Danh sách phát đã thay đổi."};
            }
            track = tracks_[index];
        }

        NotifyPlayer::InputFactory input;
        if (auto result = storage_.CreateAudioInput(track.path, input); !result) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation_ == generation)
                active_ = false;
            ESP_LOGE(TAG, "SD sequence stopped at %s: %s", track.path.c_str(),
                     result.message.c_str());
            return result;
        }

        MediaPlaybackMetadata metadata;
        storage_.GetMusicMetadata(track.path, metadata);
        auto self = shared_from_this();
        auto finished = [self, generation, index](uint32_t, bool success) {
            self->OnTrackFinished(generation, index, success);
        };
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_ || generation_ != generation || current_index_ != index)
                return {mrkenddo::SdStorage::Code::Busy, "Danh sách phát đã bị thay thế."};
            application_.RequestLocalMedia(std::move(metadata), std::move(input),
                                           std::move(finished));
        }
        return {};
    }

    void OnTrackFinished(uint32_t generation, size_t index, bool success) {
        size_t next_index;
        uint32_t next_generation;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_ || generation_ != generation || current_index_ != index)
                return;
            if (!success || index + 1 >= tracks_.size()) {
                active_ = false;
                return;
            }
            next_index = ++current_index_;
            next_generation = ++generation_;
        }

        if (auto result = QueueTrack(next_generation, next_index); !result)
            ESP_LOGE(TAG, "SD playlist ended: %s", result.message.c_str());
    }
};
}  // namespace

class MrkenDDoRobot : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button touch_button_;
    MrkenDDoRobotDisplay* display_ = nullptr;
    RobotDrive* drive_ = nullptr;
    mrkenddo::RobotSettings settings_;
    mrkenddo::SecondaryDisplay secondary_display_{settings_};
    mrkenddo::SdStorage sd_storage_;
    std::mutex sd_playlist_mutex_;
    std::weak_ptr<SdMusicPlaylistSession> sd_playlist_;
    std::unique_ptr<mrkenddo::RobotController> controller_;
    WebServer* web_server_ = nullptr;
    bool web_server_started_ = false;
    esp_timer_handle_t ip_display_timer_ = nullptr;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Thiết lập LCD");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Cài đặt LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, DISPLAY_PANEL_GAP_X, DISPLAY_PANEL_GAP_Y));
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        ESP_LOGI(TAG, "TFT panel geometry %dx%d, RAM gap (%d,%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT,
                 DISPLAY_PANEL_GAP_X, DISPLAY_PANEL_GAP_Y);

        display_ = new MrkenDDoRobotDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                            DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });

        // The button component waits for the double-click window before emitting
        // a single click. Keep playlist/player changes on the application task.
        touch_button_.OnClick([this]() {
            Application::GetInstance().Schedule([this]() {
                if (controller_ && controller_->TimeService().DismissAlert())
                    return;
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateNotifying) {
                    const auto playback = app.GetLocalMediaPlaybackStatus();
                    if (!playback.active)
                        return;
                    std::shared_ptr<SdMusicPlaylistSession> playlist;
                    {
                        std::lock_guard<std::mutex> lock(sd_playlist_mutex_);
                        playlist = sd_playlist_.lock();
                    }
                    if (playlist && playlist->SkipNext(playback.metadata.source_path))
                        ESP_LOGI(TAG, "Touch skipped to the next SD playlist track");
                    return;
                }
                if (app.GetDeviceState() != kDeviceStateIdle || controller_ == nullptr) {
                    return;
                }
                static constexpr int kReactions[] = {1, 15, 4,
                                                     8};  // happy, playful, surprised, laugh
                auto config = controller_->GetFaceConfig();
                size_t next_reaction = 0;
                for (size_t i = 0; i < sizeof(kReactions) / sizeof(kReactions[0]); ++i) {
                    if (kReactions[i] == config.idle_emotion) {
                        next_reaction = (i + 1) % (sizeof(kReactions) / sizeof(kReactions[0]));
                        break;
                    }
                }
                config.idle_emotion = kReactions[next_reaction];
                if (!controller_->ConfigureFace(config)) {
                    ESP_LOGW(TAG, "Khong the luu bieu cam mac dinh tu cam bien cham");
                    return;
                }
                app.PlaySound(Lang::Sounds::OGG_POPUP);
            });
        });

        touch_button_.OnDoubleClick([this]() {
            if (controller_ && controller_->TimeService().DismissAlert())
                return;
            auto& app = Application::GetInstance();
            const auto state = app.GetDeviceState();
            if (state == kDeviceStateRadio) {
                ClearSdPlaylist();
                app.RequestStopRadio();
                return;
            }
            if (state == kDeviceStateNotifying) {
                // Stops online or local media only; RequestStopMedia leaves
                // unrelated notification sounds alone.
                ClearSdPlaylist();
                app.RequestStopMedia();
                return;
            }
            app.Schedule([]() {
                auto& app = Application::GetInstance();
                const auto state = app.GetDeviceState();
                if (state == kDeviceStateIdle) {
                    app.ToggleChatState();
                } else if (state == kDeviceStateListening) {
                    app.StopListening();
                } else if (state == kDeviceStateSpeaking) {
                    app.StartListening();
                }
            });
        });

        touch_button_.OnLongPress([this]() {
            // Stop the wheels immediately, including an active routine or an
            // emotion pulse. Do not route this safety action through the UI queue.
            if (drive_ != nullptr) {
                drive_->Stop();
            }
        });
    }

    void SetSdPlaylist(const std::shared_ptr<SdMusicPlaylistSession>& playlist) {
        std::lock_guard<std::mutex> lock(sd_playlist_mutex_);
        if (auto previous = sd_playlist_.lock(); previous && previous != playlist)
            previous->Cancel();
        sd_playlist_ = playlist;
    }

    void ClearSdPlaylist(const std::shared_ptr<SdMusicPlaylistSession>& expected = nullptr) {
        std::lock_guard<std::mutex> lock(sd_playlist_mutex_);
        auto current = sd_playlist_.lock();
        if (!expected || current == expected) {
            if (current)
                current->Cancel();
            sd_playlist_.reset();
        }
    }

    void ShowIpAddress() {
        const std::string address = controller_->ShowIp();
        if (!address.empty()) {
            ESP_LOGI(TAG, "IP Webserver: %s", address.c_str());
            Application::GetInstance().ReportBootEvent(BootStage::IpAcquired, address);
        } else {
            ESP_LOGW(TAG, "WiFi chua ket noi trong ShowIpAddress");
        }
    }

    void ScheduleIpAddressDisplay() {
        if (ip_display_timer_ == nullptr) {
            return;
        }

        // Show it once more after activation has normally finished. This is
        // intentionally delayed because HandleActivationDoneEvent replaces
        // the notification while the protocol is being initialized.
        esp_timer_stop(ip_display_timer_);
        ESP_ERROR_CHECK(esp_timer_start_once(ip_display_timer_, 5000000));
    }

    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        ESP_LOGI(TAG, "SetNetworkEventCallback duoc goi");
        // Chain the callbacks: first call the passed callback, then our own
        WifiBoard::SetNetworkEventCallback(
            [this, callback](NetworkEvent event, const std::string& data) {
                ESP_LOGI(TAG, "Su kien mang: %d", static_cast<int>(event));

                // Call the original callback (from Application)
                if (callback) {
                    callback(event, data);
                }

                // Handle web server start on connection
                if (event == NetworkEvent::Connected) {
                    ESP_LOGI(TAG, "WiFi da ket noi, dang khoi dong web server");
                    Application::GetInstance().Schedule([this]() {
                        if (web_server_ != nullptr) {
                            if (!web_server_started_) {
                                ESP_LOGI(TAG, "Dang khoi dong web server tren cong 80");
                                web_server_started_ = web_server_->Start(80);
                            }
                            ShowIpAddress();
                            ScheduleIpAddressDisplay();
                        }
                    });
                }
            });
    }

    void SetTftRotation(int degrees) {
        if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
            return;
        }
        if (!lvgl_port_lock(1000)) {
            ESP_LOGE(TAG, "Khong the khoa LVGL de xoay TFT");
            return;
        }
        lv_display_t* lcd = lv_display_get_default();
        if (lcd != nullptr) {
            lv_display_set_rotation(lcd, static_cast<lv_display_rotation_t>(degrees / 90));
            // LVGL's resolution update invalidates the system layer, but the
            // ST7789 retains old pixels outside the next partial refresh.
            // Redraw the entire active screen after changing MADCTL.
            lv_obj_invalidate(lv_display_get_screen_active(lcd));
            lv_obj_invalidate(lv_display_get_layer_top(lcd));
            lv_obj_invalidate(lv_display_get_layer_sys(lcd));
            // Runtime changes happen while the display is using partial
            // refresh. Flush the invalidated frame immediately so pixels
            // rendered with the previous MADCTL orientation are not left on
            // the panel beside the newly rotated content.
            lv_refr_now(lcd);
            ESP_LOGI(TAG, "TFT rotation applied: %d degrees", degrees);
        }
        lvgl_port_unlock();
    }

protected:
    const char* GetWifiConfigSsidPrefix() const override { return "MrKenDDo-Robot"; }

public:
    bool HandleDeviceBridgeCommand(const std::string& payload_json, std::string* result_json,
                                   std::string* error) override {
        auto reject = [error](const char* message) {
            if (error)
                *error = message;
            return false;
        };
        cJSON* root = cJSON_ParseWithLength(payload_json.data(), payload_json.size());
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
        if (!cJSON_IsObject(root) || !cJSON_IsString(type) || !type->valuestring ||
            std::strncmp(type->valuestring, "sd.", 3) != 0) {
            cJSON_Delete(root);
            return reject("unsupported board command");
        }
        const std::string command(type->valuestring);
        cJSON* response = cJSON_CreateObject();
        bool accepted = false;
        if (command == "sd.status") {
            const auto status = sd_storage_.GetStatus();
            cJSON_AddStringToObject(response, "state", status.state.c_str());
            cJSON_AddStringToObject(response, "message", status.message.c_str());
            cJSON_AddNumberToObject(response, "free_bytes", static_cast<double>(status.free_bytes));
            cJSON_AddBoolToObject(response, "space_valid", status.space_valid);
            cJSON_AddStringToObject(response, "music_root", mrkenddo::SdStorage::kMusicRoot);
            cJSON_AddStringToObject(response, "music_index_state",
                                    status.music_index_state.c_str());
            cJSON_AddStringToObject(response, "music_index_message",
                                    status.music_index_message.c_str());
            cJSON_AddNumberToObject(response, "music_tracks", status.music_tracks);
            cJSON_AddBoolToObject(response, "music_truncated", status.music_truncated);
            cJSON_AddNumberToObject(response, "music_generation", status.music_generation);
            accepted = true;
        } else if (command == "sd.search") {
            auto get_text = [&](const char* key, size_t max, std::string& value) {
                const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
                if (!item)
                    return true;
                if (!cJSON_IsString(item) || !item->valuestring ||
                    std::strlen(item->valuestring) > max)
                    return false;
                value = item->valuestring;
                return true;
            };
            std::string query, artist, album, genre;
            const cJSON* limit_json = cJSON_GetObjectItemCaseSensitive(root, "limit");
            int limit = cJSON_IsNumber(limit_json) ? limit_json->valueint : 5;
            if (!get_text("query", 96, query) || !get_text("artist", 96, artist) ||
                !get_text("album", 96, album) || !get_text("genre", 96, genre) || limit < 1 ||
                limit > 10) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("invalid SD search filters or limit");
            }
            std::vector<mrkenddo::SdStorage::MusicTrack> tracks;
            bool truncated = false;
            auto search = sd_storage_.SearchMusic(query, artist, album, genre,
                                                  static_cast<size_t>(limit), tracks, truncated);
            if (!search) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(search.message.c_str());
            }
            cJSON_AddBoolToObject(response, "truncated", truncated);
            cJSON* results = cJSON_AddArrayToObject(response, "results");
            if (!results) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("out of memory creating SD search results");
            }
            for (const auto& track : tracks) {
                cJSON* item = cJSON_CreateObject();
                if (!item)
                    continue;
                cJSON_AddStringToObject(item, "path", track.path.c_str());
                cJSON_AddStringToObject(item, "title", track.title.c_str());
                cJSON_AddStringToObject(item, "artist", track.artist.c_str());
                cJSON_AddStringToObject(item, "album", track.album.c_str());
                cJSON_AddStringToObject(item, "genre", track.genre.c_str());
                cJSON_AddNumberToObject(item, "duration_ms", track.duration_ms);
                cJSON_AddNumberToObject(item, "size", static_cast<double>(track.size));
                cJSON_AddItemToArray(results, item);
            }
            accepted = true;
        } else if (command == "sd.photo.search") {
            auto get_text = [&](const char* key, size_t max, std::string& value) {
                const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
                if (!item)
                    return true;
                if (!cJSON_IsString(item) || !item->valuestring ||
                    std::strlen(item->valuestring) > max)
                    return false;
                value = item->valuestring;
                return true;
            };
            std::string query, album;
            const cJSON* limit_json = cJSON_GetObjectItemCaseSensitive(root, "limit");
            int limit = cJSON_IsNumber(limit_json) ? limit_json->valueint : 5;
            if (!get_text("query", 96, query) || !get_text("album", 96, album) || limit < 1 ||
                limit > 10) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("invalid photo search filters or limit");
            }
            std::vector<mrkenddo::SdStorage::Photo> photos;
            bool truncated = false;
            auto search = sd_storage_.SearchPhotos(query, album, static_cast<size_t>(limit), photos,
                                                   truncated);
            if (!search) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(search.message.c_str());
            }
            cJSON_AddBoolToObject(response, "truncated", truncated);
            cJSON_AddNumberToObject(response, "count", photos.size());
            cJSON* results = cJSON_AddArrayToObject(response, "results");
            if (!results) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("out of memory creating photo search results");
            }
            for (const auto& photo : photos) {
                cJSON* item = cJSON_CreateObject();
                if (!item)
                    continue;
                cJSON_AddStringToObject(item, "path", photo.path.c_str());
                cJSON_AddStringToObject(item, "name", photo.name.c_str());
                cJSON_AddStringToObject(item, "album", photo.album.c_str());
                cJSON_AddNumberToObject(item, "size", static_cast<double>(photo.size));
                cJSON_AddItemToArray(results, item);
            }
            accepted = true;
        } else if (command == "sd.photo.show") {
            const cJSON* path_json = cJSON_GetObjectItemCaseSensitive(root, "path");
            const cJSON* duration_json = cJSON_GetObjectItemCaseSensitive(root, "duration_seconds");
            const int duration_seconds =
                cJSON_IsNumber(duration_json) ? duration_json->valueint : 30;
            if (!cJSON_IsString(path_json) || !path_json->valuestring ||
                std::strlen(path_json->valuestring) > 192 || duration_seconds < 5 ||
                duration_seconds > 120) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("sd.photo.show requires a photo path and duration 5..120 seconds");
            }
            std::string error_message;
            if (!web_server_ || !web_server_->ShowSdPhoto(path_json->valuestring,
                                                          duration_seconds * 1000, error_message)) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(error_message.empty() ? "HTTP/photo service unavailable"
                                                    : error_message.c_str());
            }
            cJSON_AddStringToObject(response, "status", "queued");
            cJSON_AddNumberToObject(response, "duration_seconds", duration_seconds);
            accepted = true;
        } else if (command == "sd.album.random") {
            const cJSON* album_json = cJSON_GetObjectItemCaseSensitive(root, "album");
            const cJSON* interval_json = cJSON_GetObjectItemCaseSensitive(root, "interval_seconds");
            const cJSON* count_json = cJSON_GetObjectItemCaseSensitive(root, "max_photos");
            const std::string album = cJSON_IsString(album_json) && album_json->valuestring
                                          ? album_json->valuestring
                                          : "";
            const int interval_seconds =
                cJSON_IsNumber(interval_json) ? interval_json->valueint : 15;
            const int max_photos = cJSON_IsNumber(count_json) ? count_json->valueint : 10;
            if (album.size() > 96 || interval_seconds < 5 || interval_seconds > 60 ||
                max_photos < 1 || max_photos > 50) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("album slideshow limits: interval 5..60, max_photos 1..50");
            }
            std::string error_message;
            if (!web_server_ ||
                !web_server_->StartSdSlideshow(album, interval_seconds * 1000,
                                               static_cast<size_t>(max_photos), error_message)) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(error_message.empty() ? "HTTP/photo service unavailable"
                                                    : error_message.c_str());
            }
            cJSON_AddStringToObject(response, "status", "started");
            cJSON_AddNumberToObject(response, "interval_seconds", interval_seconds);
            cJSON_AddNumberToObject(response, "max_photos", max_photos);
            accepted = true;
        } else if (command == "sd.album.stop") {
            if (!web_server_) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("photo service unavailable");
            }
            web_server_->StopSdSlideshow();
            cJSON_AddStringToObject(response, "status", "stopped");
            accepted = true;
        } else if (command == "sd.album.play") {
            const cJSON* album_json = cJSON_GetObjectItemCaseSensitive(root, "album");
            if (!cJSON_IsString(album_json) || !album_json->valuestring ||
                std::strlen(album_json->valuestring) == 0 ||
                std::strlen(album_json->valuestring) > 96) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("sd.album.play requires an album name");
            }
            std::vector<mrkenddo::SdStorage::MusicTrack> album_tracks;
            auto search = sd_storage_.GetAlbumMusicTracks(album_json->valuestring, album_tracks);
            if (!search) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(search.message.c_str());
            }
            auto playlist = std::make_shared<SdMusicPlaylistSession>(
                sd_storage_, Application::GetInstance(), std::move(album_tracks));
            SetSdPlaylist(playlist);
            auto play = playlist->Start();
            if (!play) {
                ClearSdPlaylist(playlist);
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(play.message.c_str());
            }
            cJSON_AddStringToObject(response, "status", "queued");
            cJSON_AddStringToObject(response, "album", album_json->valuestring);
            cJSON_AddNumberToObject(response, "tracks", playlist->TrackCount());
            accepted = true;
        } else if (command == "sd.artist.play") {
            const cJSON* artist_json = cJSON_GetObjectItemCaseSensitive(root, "artist");
            if (!cJSON_IsString(artist_json) || !artist_json->valuestring ||
                std::strlen(artist_json->valuestring) == 0 ||
                std::strlen(artist_json->valuestring) > 96) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("sd.artist.play requires an artist name");
            }
            std::vector<mrkenddo::SdStorage::MusicTrack> artist_tracks;
            auto search = sd_storage_.GetArtistMusicTracks(artist_json->valuestring, artist_tracks);
            if (!search) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(search.message.c_str());
            }
            auto playlist = std::make_shared<SdMusicPlaylistSession>(
                sd_storage_, Application::GetInstance(), std::move(artist_tracks));
            SetSdPlaylist(playlist);
            auto play = playlist->Start();
            if (!play) {
                ClearSdPlaylist(playlist);
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(play.message.c_str());
            }
            cJSON_AddStringToObject(response, "status", "queued");
            cJSON_AddStringToObject(response, "artist", artist_json->valuestring);
            cJSON_AddNumberToObject(response, "tracks", playlist->TrackCount());
            accepted = true;
        } else if (command == "sd.play") {
            const cJSON* path_json = cJSON_GetObjectItemCaseSensitive(root, "path");
            if (!cJSON_IsString(path_json) || !path_json->valuestring ||
                std::strlen(path_json->valuestring) > 192) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("sd.play requires a valid path from sd.search");
            }
            const std::string path(path_json->valuestring);
            NotifyPlayer::InputFactory input;
            auto play = sd_storage_.CreateAudioInput(path, input);
            if (!play) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject(play.message.c_str());
            }
            MediaPlaybackMetadata metadata;
            const cJSON* title = cJSON_GetObjectItemCaseSensitive(root, "title");
            const cJSON* artist = cJSON_GetObjectItemCaseSensitive(root, "artist");
            if ((title && (!cJSON_IsString(title) || !title->valuestring ||
                           std::strlen(title->valuestring) > 192)) ||
                (artist && (!cJSON_IsString(artist) || !artist->valuestring ||
                            std::strlen(artist->valuestring) > 128))) {
                cJSON_Delete(response);
                cJSON_Delete(root);
                return reject("sd.play metadata is invalid or too long");
            }
            sd_storage_.GetMusicMetadata(path, metadata);
            ClearSdPlaylist();
            if (cJSON_IsString(title) && title->valuestring && title->valuestring[0])
                metadata.title = title->valuestring;
            if (cJSON_IsString(artist) && artist->valuestring && artist->valuestring[0])
                metadata.artist = artist->valuestring;
            Application::GetInstance().RequestLocalMedia(std::move(metadata), std::move(input));
            cJSON_AddStringToObject(response, "status", "queued");
            cJSON_AddStringToObject(response, "path", path.c_str());
            accepted = true;
        } else if (command == "sd.stop") {
            ClearSdPlaylist();
            Application::GetInstance().RequestStopMedia();
            cJSON_AddStringToObject(response, "status", "queued");
            accepted = true;
        }

        if (accepted && result_json) {
            char* encoded = cJSON_PrintUnformatted(response);
            if (encoded) {
                *result_json = encoded;
                cJSON_free(encoded);
            }
        } else if (!accepted && error) {
            *error = "unsupported SD command";
        }
        ESP_LOGI(TAG, "Device bridge %s: %s", command.c_str(), accepted ? "accepted" : "rejected");
        cJSON_Delete(response);
        cJSON_Delete(root);
        return accepted;
    }

    MrkenDDoRobot()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
          touch_button_(TOUCH_SENSOR_GPIO, true, 1200, 350) {
        ESP_LOGI(TAG, "Ham tao MrkenDDoRobot bat dau");
        esp_timer_create_args_t ip_display_timer_args = {
            .callback =
                [](void* arg) {
                    auto* board = static_cast<MrkenDDoRobot*>(arg);
                    Application::GetInstance().Schedule([board]() { board->ShowIpAddress(); });
                },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "show_ip_address",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&ip_display_timer_args, &ip_display_timer_));
        InitializeSpi();
        InitializeSt7789Display();
        // SpiLcdDisplay creates the LVGL display before Application::Initialize
        // creates any widgets. Apply the saved rotation at this point so the
        // first frame is rendered in one coordinate system; changing it after
        // SetupUI() leaves partial-refresh regions from the old orientation.
        SetTftRotation(settings_.LoadTftRotation());
        secondary_display_.Initialize();
        InitializeButtons();
        ESP_LOGI(TAG, "Dang khoi tao hai dong co DC");
        drive_ = new RobotDrive(STEPPER_INT1_GPIO, STEPPER_INT2_GPIO, STEPPER_INT3_GPIO,
                                STEPPER_INT4_GPIO);
        controller_ = std::make_unique<mrkenddo::RobotController>(
            settings_, *drive_, secondary_display_, *display_,
            [this](int rotation) { SetTftRotation(rotation); }, sd_storage_);
        RegisterRobotMcpTools(*controller_);
        // Set default volume (0-100)
        GetAudioCodec()->SetOutputVolume(60);
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }

        // Create web server (start it when WiFi connects)
        ESP_LOGI(TAG, "Dang tao web server");
        web_server_ = new WebServer(*controller_);

        ESP_LOGI(TAG, "Ham tao MrkenDDoRobot hoan tat");
    }

    ~MrkenDDoRobot() override {
        if (ip_display_timer_ != nullptr) {
            esp_timer_stop(ip_display_timer_);
            esp_timer_delete(ip_display_timer_);
            ip_display_timer_ = nullptr;
        }
        // Adapters/controllers must release their references before hardware is destroyed.
        delete web_server_;
        web_server_ = nullptr;
        controller_.reset();
        delete drive_;
        drive_ = nullptr;
        delete display_;
        display_ = nullptr;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                                               AUDIO_I2S_SPK_GPIO_DOUT, I2S_STD_SLOT_LEFT,
                                               AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS,
                                               AUDIO_I2S_MIC_GPIO_DIN, AUDIO_I2S_MIC_SLOT_MASK);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual void OnApplicationReady() override { sd_storage_.Start(); }

    void OnBootEvent(const BootEvent& event) override { secondary_display_.OnBootEvent(event); }

    BootAudioConfig GetBootAudioConfig() override { return settings_.LoadBootAudio(); }

    virtual std::string_view GetBootSound(BootStage stage) override {
        switch (stage) {
            case BootStage::SystemInitializing:
                return Lang::Sounds::OGG_ROBOT_STARTING;
            case BootStage::WifiScanning:
                return Lang::Sounds::OGG_VIBRATION;
            case BootStage::WifiConnecting:
                return Lang::Sounds::OGG_POPUP;
            case BootStage::IpAcquired:
                return Lang::Sounds::OGG_SUCCESS;
            case BootStage::Ready:
                return Lang::Sounds::OGG_ROBOT_EYES_WAKE;
            case BootStage::Error:
                return Lang::Sounds::OGG_EXCLAMATION;
            case BootStage::WifiConnected:
            case BootStage::CheckingVersion:
            case BootStage::LoggingIn:
                return {};
        }
        return {};
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(MrkenDDoRobot);
