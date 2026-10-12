#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "stepper_motor.h"
#include "robot_mcp_tools.h"
#include "web_server.h"

#include <esp_log.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <esp_sntp.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <nvs.h>

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
    StepperMotor* stepper_motor_;
    WebServer* web_server_;
    bool web_server_started_ = false;
    
    // Second OLED display
    i2c_master_bus_handle_t second_display_i2c_bus_;
    esp_lcd_panel_io_handle_t second_panel_io_ = nullptr;
    esp_lcd_panel_handle_t second_panel_ = nullptr;
    TaskHandle_t time_display_task_handle_ = nullptr;

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
        
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeSecondDisplayI2c() {
        ESP_LOGI(TAG, "Khoi tao I2C cho man hinh thu hai");
        
        // Load OLED config from NVS
        char oled_addr[8] = "3C";
        int32_t oled_sda = 2;
        int32_t oled_scl = 1;
        
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("oled_config", NVS_READONLY, &nvs_handle);
        if (err == ESP_OK) {
            size_t len = sizeof(oled_addr);
            nvs_get_str(nvs_handle, "addr", oled_addr, &len);
            nvs_get_i32(nvs_handle, "sda", &oled_sda);
            nvs_get_i32(nvs_handle, "scl", &oled_scl);
            nvs_close(nvs_handle);
            ESP_LOGI(TAG, "Da tai cau hinh OLED tu NVS: addr=%s, sda=%d, scl=%d", oled_addr, oled_sda, oled_scl);
        } else {
            ESP_LOGW(TAG, "Khong tim thay cau hinh OLED, su dung mac dinh");
        }
        
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)1,  // Use I2C port 1 (different from main display if any)
            .sda_io_num = (gpio_num_t)oled_sda,
            .scl_io_num = (gpio_num_t)oled_scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &second_display_i2c_bus_));
    }

    void InitializeSecondOledDisplay() {
        ESP_LOGI(TAG, "Khoi tao man hinh OLED thu hai 128x32");
        
        // Load OLED config from NVS
        char oled_addr[8] = "3C";
        int32_t oled_sda = 2;
        int32_t oled_scl = 1;
        int32_t oled_width = 128;
        int32_t oled_height = 32;
        
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("oled_config", NVS_READONLY, &nvs_handle);
        if (err == ESP_OK) {
            size_t len = sizeof(oled_addr);
            nvs_get_str(nvs_handle, "addr", oled_addr, &len);
            nvs_get_i32(nvs_handle, "sda", &oled_sda);
            nvs_get_i32(nvs_handle, "scl", &oled_scl);
            nvs_get_i32(nvs_handle, "width", &oled_width);
            nvs_get_i32(nvs_handle, "height", &oled_height);
            nvs_close(nvs_handle);
            ESP_LOGI(TAG, "Da tai cau hinh OLED tu NVS: addr=%s, sda=%d, scl=%d, width=%d, height=%d", 
                     oled_addr, oled_sda, oled_scl, oled_width, oled_height);
        } else {
            ESP_LOGW(TAG, "Khong tim thay cau hinh OLED, su dung mac dinh");
        }
        
        // Convert hex address to integer
        int addr_int = strtol(oled_addr, NULL, 16);
        
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = (uint8_t)addr_int,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        };

        esp_err_t ret = esp_lcd_new_panel_io_i2c(second_display_i2c_bus_, &io_config, &second_panel_io_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the tao panel IO I2C: %s", esp_err_to_name(ret));
            return;
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = (uint8_t)oled_height,
        };
        panel_config.vendor_config = &ssd1306_config;

        ret = esp_lcd_new_panel_ssd1306(second_panel_io_, &panel_config, &second_panel_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the tao panel SSD1306: %s", esp_err_to_name(ret));
            return;
        }
        
        ret = esp_lcd_panel_reset(second_panel_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the reset panel: %s", esp_err_to_name(ret));
            return;
        }
        
        ret = esp_lcd_panel_init(second_panel_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the khoi tao man hinh thu hai: %s", esp_err_to_name(ret));
            return;
        }

        ret = esp_lcd_panel_disp_on_off(second_panel_, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khong the bat man hinh: %s", esp_err_to_name(ret));
            return;
        }
        
        ESP_LOGI(TAG, "Man hinh OLED thu hai khoi tao thanh cong");
        
        // Draw test pattern (alternating pixels to verify display works)
        uint8_t* test_buffer = (uint8_t*)calloc(oled_width * oled_height / 8, 1);
        // Fill with alternating pattern (0xAA = 10101010 in binary)
        for (int i = 0; i < oled_width * oled_height / 8; i++) {
            test_buffer[i] = 0xAA;
        }
        esp_lcd_panel_draw_bitmap(second_panel_, 0, 0, oled_width, oled_height, test_buffer);
        free(test_buffer);
        ESP_LOGI(TAG, "Da ve test pattern len man hinh");
    }

    static void TimeDisplayTask(void* pvParameters) {
        MrkenDDoRobot* robot = static_cast<MrkenDDoRobot*>(pvParameters);
        
        // Static buffer for display bitmap (128x32 pixels = 512 bytes)
        static uint8_t display_buffer[SECOND_DISPLAY_WIDTH * SECOND_DISPLAY_HEIGHT / 8];
        memset(display_buffer, 0, sizeof(display_buffer));
        
        while (true) {
            if (robot->second_panel_) {
                time_t now;
                struct tm timeinfo;
                time(&now);
                localtime_r(&now, &timeinfo);
                
                // Format time string: HH:MM:SS
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
                
                // Don't clear display - keep test pattern visible for now
                // TODO: Add font rendering to display actual time text
            }
            
            vTaskDelay(pdMS_TO_TICKS(1000));  // Update every second
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
            GetDisplay()->ShowNotification(ip);
        } else {
            ESP_LOGW(TAG, "WiFi chua ket noi trong ShowIpAddress");
        }
    }

    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        ESP_LOGI(TAG, "SetNetworkEventCallback duoc goi");
        // Chain the callbacks: first call the passed callback, then our own
        WifiBoard::SetNetworkEventCallback([this, callback](NetworkEvent event, const std::string& data) {
            ESP_LOGI(TAG, "Su kien mang: %d", static_cast<int>(event));
            
            // Call the original callback (from Application)
            if (callback) {
                callback(event, data);
            }
            
            // Handle web server start on connection
            if (event == NetworkEvent::Connected && !web_server_started_) {
                ESP_LOGI(TAG, "WiFi da ket noi, dang khoi dong web server");
                web_server_started_ = true;
                Application::GetInstance().Schedule([this]() {
                    ShowIpAddress();
                    ESP_LOGI(TAG, "Dang khoi dong web server tren cong 80");
                    web_server_->Start(80);
                });
            }
        });
    }

public:
    void ReinitializeSecondOledDisplay() {
        ESP_LOGI(TAG, "Khoi tao lai man hinh OLED thu hai");
        
        // Clean up existing resources
        if (second_panel_) {
            esp_lcd_panel_del(second_panel_);
            second_panel_ = nullptr;
        }
        if (second_panel_io_) {
            esp_lcd_panel_io_del(second_panel_io_);
            second_panel_io_ = nullptr;
        }
        if (second_display_i2c_bus_) {
            i2c_del_master_bus(second_display_i2c_bus_);
            second_display_i2c_bus_ = nullptr;
        }
        
        // Reinitialize I2C bus and display
        InitializeSecondDisplayI2c();
        InitializeSecondOledDisplay();
    }
    
    MrkenDDoRobot() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Ham tao MrkenDDoRobot bat dau");
        InitializeSpi();
        InitializeSt7789Display();
        InitializeSecondDisplayI2c();
        InitializeSecondOledDisplay();
        InitializeButtons();
        ESP_LOGI(TAG, "Dang khoi tao dong co buoc");
        stepper_motor_ = new StepperMotor(STEPPER_INT1_GPIO, STEPPER_INT2_GPIO, 
                                           STEPPER_INT3_GPIO, STEPPER_INT4_GPIO);
        RegisterRobotMcpTools(stepper_motor_);
        // Set default volume (0-100)
        GetAudioCodec()->SetOutputVolume(40);
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
        // Create web server (start it when WiFi connects)
        ESP_LOGI(TAG, "Dang tao web server");
        web_server_ = new WebServer(stepper_motor_);
        
        // Set global pointer for web server access
        g_robot_board = this;
        
        // Start time display task for second OLED
        ESP_LOGI(TAG, "Khoi dong task hien thi thoi gian");
        xTaskCreate(TimeDisplayTask, "TimeDisplay", 4096, this, 5, &time_display_task_handle_);
        
        ESP_LOGI(TAG, "Ham tao MrkenDDoRobot hoan tat");
    }

    ~MrkenDDoRobot() {
        if (web_server_) {
            delete web_server_;
        }
        if (stepper_motor_) {
            delete stepper_motor_;
        }
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, 
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    StepperMotor* GetStepperMotor() {
        return stepper_motor_;
    }
};

DECLARE_BOARD(MrkenDDoRobot);

// C wrapper function for web server to call OLED reinitialization
extern "C" void ReinitializeOledDisplay() {
    if (g_robot_board) {
        g_robot_board->ReinitializeSecondOledDisplay();
    }
}
