#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "led/single_led.h"
#include "oled_clock_font.h"
#include "robot_mcp_tools.h"
#include "robot_drive.h"
#include "system_reset.h"
#include "web_server.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_timer.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <soc/soc_caps.h>
#include <time.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <wifi_manager.h>

#define TAG "MRKENDDO_ROBOT"

class MrkenDDoRobot;

static MrkenDDoRobot* g_robot_board = nullptr;

class MrkenDDoRobot : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    LcdDisplay* display_;
    RobotDrive* drive_ = nullptr;
    WebServer* web_server_;
    bool web_server_started_ = false;
    esp_timer_handle_t ip_display_timer_ = nullptr;

    // Second OLED display
    i2c_master_bus_handle_t second_display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t second_panel_io_ = nullptr;
    esp_lcd_panel_handle_t second_panel_ = nullptr;
    uint8_t* second_display_framebuffer_ = nullptr;
    size_t second_display_framebuffer_size_ = 0;
    int second_display_width_ = 0;
    int second_display_height_ = 0;
    int second_display_rotation_ = 0;
    TaskHandle_t time_display_task_handle_ = nullptr;
    SemaphoreHandle_t second_display_mutex_ = nullptr;

    struct OledConfig {
        char address[3];
        int32_t sda;
        int32_t scl;
        int32_t width;
        int32_t height;
        int32_t rotation = 0;
    };

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
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                     DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    OledConfig DefaultOledConfig() const {
        OledConfig config = {{'\0', '\0', '\0'},
                             SECOND_DISPLAY_SDA_PIN,
                             SECOND_DISPLAY_SCL_PIN,
                             SECOND_DISPLAY_WIDTH,
                             SECOND_DISPLAY_HEIGHT};
        std::snprintf(config.address, sizeof(config.address), "%02X", SECOND_DISPLAY_I2C_ADDRESS);
        return config;
    }

    bool ValidateOledConfig(const OledConfig& config) const {
        char* end = nullptr;
        long address = std::strtol(config.address, &end, 16);
        return end != config.address && *end == '\0' && address >= 0x03 && address <= 0x77 &&
               config.sda >= 0 && config.scl >= 0 && config.sda < SOC_GPIO_PIN_COUNT &&
               config.scl < SOC_GPIO_PIN_COUNT && config.sda != config.scl && config.width >= 64 &&
               config.width <= 256 && config.width % 8 == 0 && config.height >= 16 &&
               config.height <= 64 && config.height % 8 == 0;
    }

    OledConfig LoadOledConfig() const {
        OledConfig config = DefaultOledConfig();
        nvs_handle_t handle;
        esp_err_t err = nvs_open("oled_config", NVS_READONLY, &handle);
        if (err == ESP_OK) {
            size_t address_length = sizeof(config.address);
            bool complete = nvs_get_str(handle, "addr", config.address, &address_length) == ESP_OK;
            complete = nvs_get_i32(handle, "sda", &config.sda) == ESP_OK && complete;
            complete = nvs_get_i32(handle, "scl", &config.scl) == ESP_OK && complete;
            complete = nvs_get_i32(handle, "width", &config.width) == ESP_OK && complete;
            complete = nvs_get_i32(handle, "height", &config.height) == ESP_OK && complete;
            int32_t rotation = 0;
            if (nvs_get_i32(handle, "rotation", &rotation) == ESP_OK &&
                (rotation == 0 || rotation == 180)) {
                config.rotation = rotation;
            }
            nvs_close(handle);
            if (!complete || !ValidateOledConfig(config)) {
                ESP_LOGW(TAG, "Cau hinh OLED trong NVS khong hop le, dung mac dinh");
                config = DefaultOledConfig();
            } else {
                ESP_LOGI(TAG, "Da tai cau hinh OLED: addr=%s, sda=%d, scl=%d, width=%d, height=%d",
                         config.address, config.sda, config.scl, config.width, config.height);
            }
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Khong the doc cau hinh OLED tu NVS: %s; dung mac dinh",
                     esp_err_to_name(err));
        }
        return config;
    }

    void DestroySecondDisplayPanel() {
        free(second_display_framebuffer_);
        second_display_framebuffer_ = nullptr;
        second_display_framebuffer_size_ = 0;
        second_display_width_ = 0;
        second_display_height_ = 0;
        if (second_panel_) {
            esp_lcd_panel_disp_on_off(second_panel_, false);
            esp_lcd_panel_del(second_panel_);
            second_panel_ = nullptr;
        }
        if (second_panel_io_) {
            esp_lcd_panel_io_del(second_panel_io_);
            second_panel_io_ = nullptr;
        }
    }

    void SetSecondOledPixel(int x, int y) {
        if (x < 0 || y < 0 || x >= second_display_width_ || y >= second_display_height_) {
            return;
        }
        if (second_display_rotation_ == 180) {
            x = second_display_width_ - 1 - x;
            y = second_display_height_ - 1 - y;
        }
        size_t offset = static_cast<size_t>(x) +
                        static_cast<size_t>(y / 8) * static_cast<size_t>(second_display_width_);
        if (offset < second_display_framebuffer_size_) {
            second_display_framebuffer_[offset] |= static_cast<uint8_t>(1U << (y % 8));
        }
    }

    bool RenderSecondOledClockLocked() {
        if (second_panel_ == nullptr || second_display_framebuffer_ == nullptr) {
            return false;
        }

        char clock_text[] = "--:--:--";
        time_t now = time(nullptr);
        tm local_time = {};
        if (now >= 1577836800 && localtime_r(&now, &local_time) != nullptr) {
            std::snprintf(clock_text, sizeof(clock_text), "%02d:%02d:%02d", local_time.tm_hour,
                          local_time.tm_min, local_time.tm_sec);
        }

        constexpr int kClockCharacters = sizeof(clock_text) - 1;
        constexpr int kGlyphSpacing = 1;
        int scale = 1;
        for (int candidate = 8; candidate >= 1; --candidate) {
            int text_width = kClockCharacters * mrkenddo::oled_clock_font::kGlyphWidth * candidate +
                             (kClockCharacters - 1) * kGlyphSpacing;
            int text_height = mrkenddo::oled_clock_font::kGlyphHeight * candidate;
            if (text_width <= second_display_width_ && text_height <= second_display_height_) {
                scale = candidate;
                break;
            }
        }

        int text_width = kClockCharacters * mrkenddo::oled_clock_font::kGlyphWidth * scale +
                         (kClockCharacters - 1) * kGlyphSpacing;
        int text_height = mrkenddo::oled_clock_font::kGlyphHeight * scale;
        int start_x = (second_display_width_ - text_width) / 2;
        int start_y = (second_display_height_ - text_height) / 2;

        std::memset(second_display_framebuffer_, 0, second_display_framebuffer_size_);
        for (int character_index = 0; character_index < kClockCharacters; ++character_index) {
            const uint8_t* glyph = mrkenddo::oled_clock_font::GetGlyph(clock_text[character_index]);
            int glyph_x =
                start_x +
                character_index * (mrkenddo::oled_clock_font::kGlyphWidth * scale + kGlyphSpacing);
            for (int row = 0; row < mrkenddo::oled_clock_font::kGlyphHeight; ++row) {
                for (int column = 0; column < mrkenddo::oled_clock_font::kGlyphWidth; ++column) {
                    if ((glyph[row] &
                         (1U << (mrkenddo::oled_clock_font::kGlyphWidth - 1 - column))) != 0) {
                        for (int y_scale = 0; y_scale < scale; ++y_scale) {
                            for (int x_scale = 0; x_scale < scale; ++x_scale) {
                                SetSecondOledPixel(glyph_x + column * scale + x_scale,
                                                   start_y + row * scale + y_scale);
                            }
                        }
                    }
                }
            }
        }

        esp_err_t ret =
            esp_lcd_panel_draw_bitmap(second_panel_, 0, 0, second_display_width_,
                                      second_display_height_, second_display_framebuffer_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the ve dong ho OLED: %s", esp_err_to_name(ret));
            return false;
        }
        return true;
    }

    bool InitializeSecondDisplayI2c(const OledConfig& config) {
        ESP_LOGI(TAG, "Khoi tao I2C cho man hinh thu hai");

        if (!ValidateOledConfig(config)) {
            ESP_LOGE(TAG, "Cau hinh OLED khong hop le");
            return false;
        }

        i2c_master_bus_config_t bus_config = {
            .i2c_port = SECOND_DISPLAY_I2C_PORT,
            .sda_io_num = static_cast<gpio_num_t>(config.sda),
            .scl_io_num = static_cast<gpio_num_t>(config.scl),
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        esp_err_t err = i2c_new_master_bus(&bus_config, &second_display_i2c_bus_);
        if (err != ESP_OK) {
            second_display_i2c_bus_ = nullptr;
            ESP_LOGE(TAG, "Khong the tao I2C bus OLED: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    bool InitializeSecondOledDisplay(const OledConfig& config) {
        ESP_LOGI(TAG, "Khoi tao man hinh OLED thu hai %dx%d", config.width, config.height);
        if (second_display_i2c_bus_ == nullptr || !ValidateOledConfig(config)) {
            ESP_LOGE(TAG, "I2C bus hoac cau hinh OLED khong san sang");
            return false;
        }

        // Convert hex address to integer
        int addr_int = static_cast<int>(std::strtol(config.address, nullptr, 16));

        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = (uint8_t)addr_int,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags =
                {
                    .dc_low_on_data = 0,
                    .disable_control_phase = 0,
                },
        };

        esp_err_t ret =
            esp_lcd_new_panel_io_i2c(second_display_i2c_bus_, &io_config, &second_panel_io_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the tao panel IO I2C: %s", esp_err_to_name(ret));
            second_panel_io_ = nullptr;
            return false;
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(config.height),
        };
        panel_config.vendor_config = &ssd1306_config;

        ret = esp_lcd_new_panel_ssd1306(second_panel_io_, &panel_config, &second_panel_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the tao panel SSD1306: %s", esp_err_to_name(ret));
            DestroySecondDisplayPanel();
            return false;
        }

        ret = esp_lcd_panel_reset(second_panel_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the reset panel: %s", esp_err_to_name(ret));
            DestroySecondDisplayPanel();
            return false;
        }

        ret = esp_lcd_panel_init(second_panel_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the khoi tao man hinh thu hai: %s", esp_err_to_name(ret));
            DestroySecondDisplayPanel();
            return false;
        }

        ret = esp_lcd_panel_disp_on_off(second_panel_, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the bat man hinh: %s", esp_err_to_name(ret));
            DestroySecondDisplayPanel();
            return false;
        }

        ESP_LOGI(TAG, "Man hinh OLED thu hai khoi tao thanh cong");

        size_t buffer_size = static_cast<size_t>(config.width) * config.height / 8;
        second_display_framebuffer_ = static_cast<uint8_t*>(calloc(buffer_size, 1));
        if (second_display_framebuffer_ == nullptr) {
            ESP_LOGE(TAG, "Khong the cap phat bo dem dong ho OLED");
            DestroySecondDisplayPanel();
            return false;
        }
        second_display_framebuffer_size_ = buffer_size;
        second_display_width_ = config.width;
        second_display_height_ = config.height;
        second_display_rotation_ = config.rotation;
        if (!RenderSecondOledClockLocked()) {
            DestroySecondDisplayPanel();
            return false;
        }
        ESP_LOGI(TAG, "Da khoi tao dong ho tren OLED");
        return true;
    }

    static void TimeDisplayTask(void* pvParameters) {
        MrkenDDoRobot* robot = static_cast<MrkenDDoRobot*>(pvParameters);

        while (true) {
            if (robot->second_display_mutex_ != nullptr) {
                xSemaphoreTake(robot->second_display_mutex_, portMAX_DELAY);
                bool display_ready = robot->RenderSecondOledClockLocked();
                xSemaphoreGive(robot->second_display_mutex_);
                if (!display_ready) {
                    ESP_LOGD(TAG, "OLED thu hai chua san sang de hien thi dong ho");
                }
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
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
    }

    void ShowIpAddress() {
        ESP_LOGI(TAG, "Ham ShowIpAddress duoc goi");
        auto& wifi = WifiManager::GetInstance();
        if (wifi.IsConnected()) {
            std::string ip = "IP: " + wifi.GetIpAddress();
            ESP_LOGI(TAG, "IP Webserver: %s", wifi.GetIpAddress().c_str());
            // The activation flow can replace the connected notification
            // immediately. Use a longer duration so the address remains
            // readable while the web server becomes ready.
            GetDisplay()->ShowNotification(ip, 10000);
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

public:
    void ReinitializeSecondOledDisplay() {
        ESP_LOGI(TAG, "Khoi tao lai man hinh OLED thu hai");

        if (second_display_mutex_ == nullptr) {
            ESP_LOGE(TAG, "Mutex OLED chua san sang");
            return;
        }
        xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
        DestroySecondDisplayPanel();
        if (second_display_i2c_bus_) {
            i2c_del_master_bus(second_display_i2c_bus_);
            second_display_i2c_bus_ = nullptr;
        }

        OledConfig config = LoadOledConfig();
        if (!InitializeSecondDisplayI2c(config) || !InitializeSecondOledDisplay(config)) {
            ESP_LOGE(TAG, "Khoi tao lai OLED that bai");
            if (second_display_i2c_bus_) {
                i2c_del_master_bus(second_display_i2c_bus_);
                second_display_i2c_bus_ = nullptr;
            }
        }
        xSemaphoreGive(second_display_mutex_);
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
            ESP_LOGI(TAG, "TFT rotation applied: %d degrees", degrees);
        }
        lvgl_port_unlock();
    }

    MrkenDDoRobot()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Ham tao MrkenDDoRobot bat dau");
        esp_timer_create_args_t ip_display_timer_args = {
            .callback = [](void* arg) {
                auto* board = static_cast<MrkenDDoRobot*>(arg);
                Application::GetInstance().Schedule([board]() { board->ShowIpAddress(); });
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "show_ip_address",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&ip_display_timer_args, &ip_display_timer_));
        second_display_mutex_ = xSemaphoreCreateMutex();
        InitializeSpi();
        InitializeSt7789Display();
        nvs_handle_t display_handle;
        if (nvs_open("tft_config", NVS_READONLY, &display_handle) == ESP_OK) {
            int32_t rotation = 0;
            if (nvs_get_i32(display_handle, "rotation", &rotation) == ESP_OK) {
                SetTftRotation(rotation);
            }
            nvs_close(display_handle);
        }
        if (second_display_mutex_ != nullptr) {
            OledConfig config = LoadOledConfig();
            if (!InitializeSecondDisplayI2c(config) || !InitializeSecondOledDisplay(config)) {
                ESP_LOGW(TAG, "OLED thu hai khong khoi tao duoc");
                DestroySecondDisplayPanel();
                if (second_display_i2c_bus_) {
                    i2c_del_master_bus(second_display_i2c_bus_);
                    second_display_i2c_bus_ = nullptr;
                }
            }
        } else {
            ESP_LOGE(TAG, "Khong the cap phat mutex OLED");
        }
        InitializeButtons();
        ESP_LOGI(TAG, "Dang khoi tao hai dong co DC");
        drive_ = new RobotDrive(STEPPER_INT1_GPIO, STEPPER_INT2_GPIO, STEPPER_INT3_GPIO,
                                STEPPER_INT4_GPIO);
        RegisterRobotMcpTools(drive_);
        // Set default volume (0-100)
        GetAudioCodec()->SetOutputVolume(40);
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }

        // Create web server (start it when WiFi connects)
        ESP_LOGI(TAG, "Dang tao web server");
        web_server_ = new WebServer(drive_);

        // Set global pointer for web server access
        g_robot_board = this;

        // Start time display task for second OLED
        ESP_LOGI(TAG, "Khoi dong task hien thi thoi gian");
        if (xTaskCreate(TimeDisplayTask, "TimeDisplay", 4096, this, 5,
                        &time_display_task_handle_) != pdPASS) {
            ESP_LOGE(TAG, "Khong the tao task hien thi OLED");
            time_display_task_handle_ = nullptr;
        }

        ESP_LOGI(TAG, "Ham tao MrkenDDoRobot hoan tat");
    }

    ~MrkenDDoRobot() {
        if (g_robot_board == this) {
            g_robot_board = nullptr;
        }
        if (time_display_task_handle_ != nullptr) {
            vTaskDelete(time_display_task_handle_);
            time_display_task_handle_ = nullptr;
        }
        if (ip_display_timer_ != nullptr) {
            esp_timer_stop(ip_display_timer_);
            esp_timer_delete(ip_display_timer_);
            ip_display_timer_ = nullptr;
        }
        if (web_server_) {
            delete web_server_;
        }
        if (drive_) {
            delete drive_;
        }
        if (second_display_mutex_ != nullptr) {
            xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
            DestroySecondDisplayPanel();
            if (second_display_i2c_bus_) {
                i2c_del_master_bus(second_display_i2c_bus_);
                second_display_i2c_bus_ = nullptr;
            }
            xSemaphoreGive(second_display_mutex_);
            vSemaphoreDelete(second_display_mutex_);
            second_display_mutex_ = nullptr;
        }
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

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    RobotDrive* GetRobotDrive() { return drive_; }
};

DECLARE_BOARD(MrkenDDoRobot);

// C wrapper function for web server to call OLED reinitialization
extern "C" void ReinitializeOledDisplay() {
    // HTTP callbacks run in the HTTP server task. OLED/I2C lifetime changes
    // are scheduled on the application task and serialized by the board mutex.
    Application::GetInstance().Schedule([]() {
        if (g_robot_board != nullptr) {
            g_robot_board->ReinitializeSecondOledDisplay();
        }
    });
}
extern "C" void ApplyTftRotation(int rotation) {
    Application::GetInstance().Schedule([rotation]() {
        if (g_robot_board != nullptr) {
            g_robot_board->SetTftRotation(rotation);
        }
    });
}
