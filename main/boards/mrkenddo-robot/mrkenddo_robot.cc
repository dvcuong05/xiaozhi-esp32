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

#include <memory>
#include <string>

#define TAG "MRKENDDO_ROBOT"

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

        // The button component handles debounce and waits for the double-click
        // window before emitting a single click. Its callbacks run off the main
        // application task, so UI and conversation actions are scheduled there.
        touch_button_.OnClick([this]() {
            Application::GetInstance().Schedule([this]() {
                auto& app = Application::GetInstance();
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

        touch_button_.OnDoubleClick([]() {
            auto& app = Application::GetInstance();
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
            [this](int rotation) { SetTftRotation(rotation); });
        RegisterRobotMcpTools(*controller_);
        // Set default volume (0-100)
        GetAudioCodec()->SetOutputVolume(40);
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

    virtual void OnApplicationReady() override {}

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
