#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "led/single_led.h"
#include "mrkenddo_robot_display.h"
#include "oled_display_controller.h"
#include "robot_drive.h"
#include "robot_mcp_tools.h"
#include "system_reset.h"
#include "web_server.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_sntp.h>
#include <esp_timer.h>
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
#include <utility>

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
    mrkenddo::OledDisplayController oled_display_controller_;
    mrkenddo::OledDisplaySettings second_display_settings_;
    int32_t second_display_contrast_ = 255;
    bool second_display_invert_ = false;
    bool boot_display_active_ = true;
    bool oled_ip_display_active_ = false;
    int64_t oled_ip_display_until_us_ = 0;
    char oled_ip_address_[16] = {};
    bool oled_text_display_active_ = false;
    int64_t oled_text_display_until_us_ = 0;
    char oled_text_display_[sizeof(mrkenddo::OledDisplaySettings::text)] = {};
    BootStage boot_stage_ = BootStage::SystemInitializing;
    char boot_stage_detail_[kBootEventDetailSize] = {};

    struct OledConfig {
        char address[3];
        int32_t sda;
        int32_t scl;
        int32_t width;
        int32_t height;
        int32_t rotation = 0;
        int32_t mode = static_cast<int32_t>(mrkenddo::OledDisplayMode::Clock);
        char text[sizeof(mrkenddo::OledDisplaySettings::text)] = {};
        int32_t speed = 2;
        int32_t font_size = 0;
        int32_t contrast = 255;
        bool invert = false;
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
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, DISPLAY_PANEL_GAP_X, DISPLAY_PANEL_GAP_Y));
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        ESP_LOGI(TAG, "TFT panel geometry %dx%d, RAM gap (%d,%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT,
                 DISPLAY_PANEL_GAP_X, DISPLAY_PANEL_GAP_Y);

        display_ = new MrkenDDoRobotDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
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
               config.height <= 64 && config.height % 8 == 0 &&
               mrkenddo::OledDisplayController::IsValidMode(config.mode) &&
               mrkenddo::OledDisplayController::IsValidSpeed(config.speed) &&
               mrkenddo::OledDisplayController::IsValidFontSize(config.font_size) &&
               config.contrast >= 0 && config.contrast <= 255 &&
               std::strlen(config.text) < sizeof(config.text);
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

            int32_t mode = static_cast<int32_t>(mrkenddo::OledDisplayMode::Clock);
            if (nvs_get_i32(handle, "mode", &mode) == ESP_OK &&
                mrkenddo::OledDisplayController::IsValidMode(mode)) {
                config.mode = mode;
            }
            size_t text_length = sizeof(config.text);
            if (nvs_get_str(handle, "text", config.text, &text_length) != ESP_OK) {
                config.text[0] = '\0';
            }
            int32_t speed = config.speed;
            if (nvs_get_i32(handle, "speed", &speed) == ESP_OK &&
                mrkenddo::OledDisplayController::IsValidSpeed(speed)) {
                config.speed = speed;
            }
            int32_t font_size = config.font_size;
            if (nvs_get_i32(handle, "font_size", &font_size) == ESP_OK &&
                mrkenddo::OledDisplayController::IsValidFontSize(font_size)) {
                config.font_size = font_size;
            }
            int32_t contrast = config.contrast;
            if (nvs_get_i32(handle, "contrast", &contrast) == ESP_OK && contrast >= 0 &&
                contrast <= 255) {
                config.contrast = contrast;
            }
            uint8_t invert = 0;
            if (nvs_get_u8(handle, "invert", &invert) == ESP_OK && invert <= 1) {
                config.invert = invert != 0;
            }
            nvs_close(handle);
            if (!complete || !ValidateOledConfig(config)) {
                ESP_LOGW(TAG, "Cau hinh OLED trong NVS khong hop le, dung mac dinh");
                config = DefaultOledConfig();
            } else {
                ESP_LOGI(TAG,
                         "Da tai cau hinh OLED: addr=%s, sda=%d, scl=%d, width=%d, height=%d, "
                         "mode=%d",
                         config.address, config.sda, config.scl, config.width, config.height,
                         config.mode);
            }
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Khong the doc cau hinh OLED tu NVS: %s; dung mac dinh",
                     esp_err_to_name(err));
        }
        return config;
    }

    void DestroySecondDisplayPanel() {
        oled_display_controller_.Disable();
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

    void SetActiveOledConfig(const OledConfig& config) {
        second_display_rotation_ = config.rotation;
        second_display_settings_.mode = static_cast<mrkenddo::OledDisplayMode>(config.mode);
        std::snprintf(second_display_settings_.text, sizeof(second_display_settings_.text), "%s",
                      config.text);
        second_display_settings_.speed = config.speed;
        second_display_settings_.font_size = config.font_size;
        second_display_contrast_ = config.contrast;
        second_display_invert_ = config.invert;
    }

    esp_err_t ApplyOledPanelSettings() {
        if (second_panel_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        esp_err_t ret = esp_lcd_panel_invert_color(second_panel_, second_display_invert_);
        if (ret != ESP_OK) {
            return ret;
        }
        uint8_t contrast = static_cast<uint8_t>(second_display_contrast_);
        // SSD1306's contrast command is common to both the 0x3C and 0x3D
        // modules supported by this board. Keep it here instead of adding a
        // second display driver just for a one-byte setting.
        return esp_lcd_panel_io_tx_param(second_panel_io_, 0x81, &contrast, sizeof(contrast));
    }

    const char* GetOledDeviceStateLabel() const {
        switch (Application::GetInstance().GetDeviceState()) {
            case kDeviceStateStarting:
                return "STARTING";
            case kDeviceStateWifiConfiguring:
                return "WIFI SETUP";
            case kDeviceStateIdle:
                return "IDLE";
            case kDeviceStateConnecting:
                return "CONNECTING";
            case kDeviceStateListening:
                return "LISTENING";
            case kDeviceStateSpeaking:
                return "SPEAKING";
            case kDeviceStateNotifying:
                return "NOTIFY";
            case kDeviceStateUpgrading:
                return "UPDATING";
            case kDeviceStateActivating:
                return "ACTIVATING";
            case kDeviceStateAudioTesting:
                return "AUDIO TEST";
            case kDeviceStateFatalError:
                return "ERROR";
            case kDeviceStateUnknown:
            default:
                return "UNKNOWN";
        }
    }

    bool RenderSecondOledLocked() {
        if (second_panel_ == nullptr || second_display_framebuffer_ == nullptr) {
            return false;
        }

        const auto& wifi = WifiManager::GetInstance();
        const bool ip_display_active =
            oled_ip_display_active_ && esp_timer_get_time() < oled_ip_display_until_us_;
        if (oled_ip_display_active_ && !ip_display_active) {
            oled_ip_display_active_ = false;
            oled_ip_address_[0] = '\0';
        }
        const bool text_display_active =
            oled_text_display_active_ && esp_timer_get_time() < oled_text_display_until_us_;
        if (oled_text_display_active_ && !text_display_active) {
            oled_text_display_active_ = false;
            oled_text_display_[0] = '\0';
        }
        if (boot_display_active_ || ip_display_active) {
            char network_line[96] = {};
            const char* address =
                ip_display_active
                    ? oled_ip_address_
                    : (wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "CONNECTING...");
            std::snprintf(network_line, sizeof(network_line), "IP: %s", address);
            if (!oled_display_controller_.RenderBoot("MrKenDDo Robot", network_line)) {
                return false;
            }
        } else {
            char status_line_1[80] = {};
            char status_line_2[32] = {};
            std::snprintf(status_line_1, sizeof(status_line_1), "WIFI %s",
                          wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "OFFLINE");
            std::snprintf(status_line_2, sizeof(status_line_2), "%s", GetOledDeviceStateLabel());

            mrkenddo::OledDisplaySettings display_settings = second_display_settings_;
            if (text_display_active) {
                display_settings.mode = mrkenddo::OledDisplayMode::Marquee;
                std::snprintf(display_settings.text, sizeof(display_settings.text), "%s",
                              oled_text_display_);
            }
            if (!oled_display_controller_.Render(display_settings, status_line_1, status_line_2,
                                                 time(nullptr),
                                                 wifi.IsConnected() ? wifi.GetRssi() : 0)) {
                return false;
            }
        }

        esp_err_t ret =
            esp_lcd_panel_draw_bitmap(second_panel_, 0, 0, second_display_width_,
                                      second_display_height_, second_display_framebuffer_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the ve noi dung OLED: %s", esp_err_to_name(ret));
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

        SetActiveOledConfig(config);
        ret = ApplyOledPanelSettings();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the ap dung cau hinh OLED: %s", esp_err_to_name(ret));
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
        oled_display_controller_.Configure(second_display_framebuffer_,
                                           second_display_framebuffer_size_, second_display_width_,
                                           second_display_height_, second_display_rotation_);
        if (!RenderSecondOledLocked()) {
            DestroySecondDisplayPanel();
            return false;
        }
        ESP_LOGI(TAG, "Da khoi tao OLED mode=%d", config.mode);
        return true;
    }

    static void TimeDisplayTask(void* pvParameters) {
        MrkenDDoRobot* robot = static_cast<MrkenDDoRobot*>(pvParameters);

        while (true) {
            int delay_ms = 1000;
            if (robot->second_display_mutex_ != nullptr) {
                xSemaphoreTake(robot->second_display_mutex_, portMAX_DELAY);
                bool display_ready = robot->RenderSecondOledLocked();
                const bool temporary_text_active =
                    robot->oled_text_display_active_ &&
                    esp_timer_get_time() < robot->oled_text_display_until_us_;
                if (temporary_text_active ||
                    robot->second_display_settings_.mode == mrkenddo::OledDisplayMode::Marquee) {
                    int speed = std::clamp<int>(robot->second_display_settings_.speed, 1, 5);
                    delay_ms = std::max(50, 260 / speed);
                } else if (robot->second_display_settings_.mode ==
                           mrkenddo::OledDisplayMode::Status) {
                    delay_ms = 500;
                }
                xSemaphoreGive(robot->second_display_mutex_);
                if (!display_ready) {
                    ESP_LOGD(TAG, "OLED thu hai chua san sang de hien thi");
                }
            }

            vTaskDelay(pdMS_TO_TICKS(delay_ms));
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
            const std::string address = wifi.GetIpAddress();
            std::string ip = "IP: " + address;
            ESP_LOGI(TAG, "IP Webserver: %s", address.c_str());
            Application::GetInstance().ReportBootEvent(BootStage::IpAcquired, address);
            ShowIpOnOled(address);
            // The activation flow can replace the connected notification
            // immediately. Use a longer duration so the address remains
            // readable while the web server becomes ready.
            GetDisplay()->ShowNotification(ip, 10000);
        } else {
            ESP_LOGW(TAG, "WiFi chua ket noi trong ShowIpAddress");
        }
    }

    void ShowIpOnOled(const std::string& address) {
        if (second_display_mutex_ == nullptr || address.empty()) {
            return;
        }
        xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
        std::snprintf(oled_ip_address_, sizeof(oled_ip_address_), "%s", address.c_str());
        oled_ip_display_active_ = true;
        oled_ip_display_until_us_ = esp_timer_get_time() + 10000000;
        RenderSecondOledLocked();
        xSemaphoreGive(second_display_mutex_);
        ESP_LOGI(TAG, "Showing local IP on secondary OLED for 10 seconds: %s", address.c_str());
    }

    void ShowTextOnOled(const std::string& text, int duration_ms) {
        if (second_display_mutex_ == nullptr || text.empty()) {
            return;
        }
        char normalized[sizeof(mrkenddo::OledDisplaySettings::text)] = {};
        mrkenddo::OledDisplayController::NormalizeText(text.c_str(), normalized,
                                                       sizeof(normalized));
        if (normalized[0] == '\0') {
            return;
        }
        xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
        std::snprintf(oled_text_display_, sizeof(oled_text_display_), "%s", normalized);
        oled_text_display_active_ = true;
        oled_text_display_until_us_ =
            esp_timer_get_time() + static_cast<int64_t>(duration_ms) * 1000;
        RenderSecondOledLocked();
        xSemaphoreGive(second_display_mutex_);
        ESP_LOGI(TAG, "Showing custom text on secondary OLED for %d ms", duration_ms);
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
    bool SetOledModeFromMcp(int mode) {
        if (!mrkenddo::OledDisplayController::IsValidMode(mode) ||
            second_display_mutex_ == nullptr) {
            return false;
        }

        nvs_handle_t handle;
        if (nvs_open("oled_config", NVS_READWRITE, &handle) != ESP_OK) {
            return false;
        }
        const esp_err_t set_result = nvs_set_i32(handle, "mode", mode);
        const esp_err_t commit_result = set_result == ESP_OK ? nvs_commit(handle) : set_result;
        nvs_close(handle);
        if (commit_result != ESP_OK) {
            return false;
        }
        ApplyOledDisplaySettings();
        return true;
    }

    std::string ShowIpAddressFromMcp() {
        auto& wifi = WifiManager::GetInstance();
        if (!wifi.IsConnected()) {
            return {};
        }
        const std::string address = wifi.GetIpAddress();
        const std::string message = "IP: " + address;
        Application::GetInstance().Schedule([this, address]() { ShowIpOnOled(address); });
        GetDisplay()->ShowNotification(message, 10000);
        ESP_LOGI(TAG, "MCP requested local IP: %s", address.c_str());
        return address;
    }

    bool ShowOledTextFromMcp(std::string text, int duration_ms) {
        if (text.empty() || duration_ms < 1000 || duration_ms > 30000 ||
            second_display_mutex_ == nullptr) {
            return false;
        }
        Application::GetInstance().Schedule(
            [this, text = std::move(text), duration_ms]() { ShowTextOnOled(text, duration_ms); });
        return true;
    }

    bool ShowPaymentQrFromMcp(int duration_ms) {
        if (display_ == nullptr || duration_ms < 5000 || duration_ms > 120000) {
            return false;
        }
        Application::GetInstance().Schedule([this, duration_ms]() {
            static_cast<MrkenDDoRobotDisplay*>(display_)->ShowPaymentQr(duration_ms);
        });
        return true;
    }

    void ApplyOledDisplaySettings() {
        if (second_display_mutex_ == nullptr) {
            ESP_LOGE(TAG, "Mutex OLED chua san sang");
            return;
        }
        xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
        OledConfig config = LoadOledConfig();
        SetActiveOledConfig(config);
        if (second_panel_ != nullptr) {
            esp_err_t ret = ApplyOledPanelSettings();
            if (ret == ESP_OK) {
                RenderSecondOledLocked();
            } else {
                ESP_LOGE(TAG, "Khong the ap dung lai cau hinh OLED: %s", esp_err_to_name(ret));
            }
        }
        xSemaphoreGive(second_display_mutex_);
    }

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
            // Runtime changes happen while the display is using partial
            // refresh. Flush the invalidated frame immediately so pixels
            // rendered with the previous MADCTL orientation are not left on
            // the panel beside the newly rotated content.
            lv_refr_now(lcd);
            ESP_LOGI(TAG, "TFT rotation applied: %d degrees", degrees);
        }
        lvgl_port_unlock();
    }

    static bool IsValidTftRotation(int rotation) {
        return rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270;
    }

    static constexpr int32_t kTftRotationConfigVersion = 4;

    MrkenDDoRobot()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
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
        second_display_mutex_ = xSemaphoreCreateMutex();
        int32_t tft_rotation = DISPLAY_DEFAULT_ROTATION;
        nvs_handle_t display_handle;
        if (nvs_open("tft_config", NVS_READWRITE, &display_handle) == ESP_OK) {
            int32_t stored_rotation = DISPLAY_DEFAULT_ROTATION;
            bool has_valid_rotation =
                nvs_get_i32(display_handle, "rotation", &stored_rotation) == ESP_OK &&
                IsValidTftRotation(stored_rotation);
            if (has_valid_rotation) {
                tft_rotation = stored_rotation;
            }

            // Firmware before this version documented 270 degrees as the
            // default for this physical mounting. In the actual ST7789
            // hardware mapping that produces the opposite quarter-turn, so
            // migrate that old default once. A later explicit user choice is
            // preserved because the version marker is then already current.
            int32_t rotation_version = 0;
            bool needs_rotation_migration =
                nvs_get_i32(display_handle, "rotation_v", &rotation_version) != ESP_OK ||
                rotation_version < kTftRotationConfigVersion;
            if (needs_rotation_migration) {
                bool migration_ok = true;
                if (has_valid_rotation && stored_rotation == 270) {
                    tft_rotation = DISPLAY_DEFAULT_ROTATION;
                    migration_ok = nvs_set_i32(display_handle, "rotation", tft_rotation) == ESP_OK;
                    if (migration_ok) {
                        ESP_LOGW(TAG, "Migrated old TFT rotation 270 to %d degrees", tft_rotation);
                    }
                }
                if (migration_ok) {
                    migration_ok = nvs_set_i32(display_handle, "rotation_v",
                                               kTftRotationConfigVersion) == ESP_OK;
                }
                if (migration_ok && nvs_commit(display_handle) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to commit TFT rotation migration");
                }
            }
            nvs_close(display_handle);
        }

        InitializeSpi();
        InitializeSt7789Display();
        // SpiLcdDisplay creates the LVGL display before Application::Initialize
        // creates any widgets. Apply the saved rotation at this point so the
        // first frame is rendered in one coordinate system; changing it after
        // SetupUI() leaves partial-refresh regions from the old orientation.
        SetTftRotation(tft_rotation);
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
        RegisterRobotMcpTools(
            drive_, [this](int mode) { return SetOledModeFromMcp(mode); },
            [this]() { return ShowIpAddressFromMcp(); },
            [this](std::string text, int duration_ms) {
                return ShowOledTextFromMcp(std::move(text), duration_ms);
            },
            [this](int duration_ms) { return ShowPaymentQrFromMcp(duration_ms); });
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

    virtual void OnApplicationReady() override {}

    virtual void OnBootEvent(const BootEvent& event) override {
        if (second_display_mutex_ == nullptr) {
            return;
        }
        xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
        boot_stage_ = event.stage;
        std::snprintf(boot_stage_detail_, sizeof(boot_stage_detail_), "%s", event.detail);
        if (event.stage == BootStage::Ready || event.stage == BootStage::Error) {
            boot_display_active_ = false;
        }
        RenderSecondOledLocked();
        xSemaphoreGive(second_display_mutex_);
    }

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
extern "C" void ApplyOledDisplaySettings() {
    Application::GetInstance().Schedule([]() {
        if (g_robot_board != nullptr) {
            g_robot_board->ApplyOledDisplaySettings();
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
