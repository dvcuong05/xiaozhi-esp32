#include "secondary_display.h"

#include "application.h"
#include "config.h"
#include "robot_settings.h"

#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace kynangso {

namespace {
constexpr char TAG[] = "SecondaryDisplay";
}

SecondaryDisplay::SecondaryDisplay(RobotSettings& settings) : settings_(settings) {}

SecondaryDisplay::~SecondaryDisplay() {
    // Let the task finish its current render and release the mutex before freeing resources.
    if (time_display_task_handle_ != nullptr) {
        stopping_.store(true);
        xTaskNotifyGive(time_display_task_handle_);
        xSemaphoreTake(render_task_stopped_, portMAX_DELAY);
        time_display_task_handle_ = nullptr;
    }
    if (second_display_mutex_ != nullptr) {
        xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
        DestroySecondDisplayPanel();
        if (second_display_i2c_bus_ != nullptr) {
            i2c_del_master_bus(second_display_i2c_bus_);
            second_display_i2c_bus_ = nullptr;
        }
        xSemaphoreGive(second_display_mutex_);
        vSemaphoreDelete(second_display_mutex_);
    }
    if (render_task_stopped_ != nullptr) {
        vSemaphoreDelete(render_task_stopped_);
    }
}

void SecondaryDisplay::Initialize() {
    if (second_display_mutex_ == nullptr) {
        second_display_mutex_ = xSemaphoreCreateMutex();
    }
    if (second_display_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Khong the cap phat mutex OLED");
        return;
    }
    Reinitialize();
    if (render_task_stopped_ == nullptr) {
        render_task_stopped_ = xSemaphoreCreateBinary();
    }
    if (render_task_stopped_ == nullptr) {
        ESP_LOGE(TAG, "Khong the cap phat semaphore dung task OLED");
        return;
    }
    if (time_display_task_handle_ == nullptr &&
        xTaskCreate(TimeDisplayTask, "TimeDisplay", 4096, this, 5, &time_display_task_handle_) !=
            pdPASS) {
        ESP_LOGE(TAG, "Khong the tao task hien thi OLED");
        time_display_task_handle_ = nullptr;
    }
}

void SecondaryDisplay::DestroySecondDisplayPanel() {
    available_.store(false);
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

void SecondaryDisplay::SetActiveOledConfig(const kynangso::OledConfig& config) {
    second_display_rotation_ = config.rotation;
    second_display_settings_.mode = static_cast<kynangso::OledDisplayMode>(config.mode);
    std::snprintf(second_display_settings_.text, sizeof(second_display_settings_.text), "%s",
                  config.text);
    second_display_settings_.speed = config.speed;
    second_display_settings_.font_size = config.font_size;
    second_display_contrast_ = config.contrast;
    second_display_invert_ = config.invert;
}

esp_err_t SecondaryDisplay::ApplyOledPanelSettings() {
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

const char* SecondaryDisplay::GetOledDeviceStateLabel() const {
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
        case kDeviceStateRadio:
            return "RADIO";
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

bool SecondaryDisplay::RenderSecondOledLocked() {
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
        const std::string connected_address =
            wifi.IsConnected() ? wifi.GetIpAddress() : "CONNECTING...";
        const char* address = ip_display_active ? oled_ip_address_ : connected_address.c_str();
        std::snprintf(network_line, sizeof(network_line), "IP: %s", address);
        if (!oled_display_controller_.RenderBoot("KyNangSo Robot", network_line)) {
            return false;
        }
    } else {
        char status_line_1[80] = {};
        char status_line_2[32] = {};
        std::snprintf(status_line_1, sizeof(status_line_1), "WIFI %s",
                      wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "OFFLINE");
        std::snprintf(status_line_2, sizeof(status_line_2), "%s", GetOledDeviceStateLabel());

        kynangso::OledDisplaySettings display_settings = second_display_settings_;
        if (text_display_active) {
            display_settings.mode = kynangso::OledDisplayMode::Marquee;
            std::snprintf(display_settings.text, sizeof(display_settings.text), "%s",
                          oled_text_display_);
        }
        if (!oled_display_controller_.Render(display_settings, status_line_1, status_line_2,
                                             time(nullptr),
                                             wifi.IsConnected() ? wifi.GetRssi() : 0)) {
            return false;
        }
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(second_panel_, 0, 0, second_display_width_,
                                              second_display_height_, second_display_framebuffer_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khong the ve noi dung OLED: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool SecondaryDisplay::InitializeSecondDisplayI2c(const kynangso::OledConfig& config) {
    ESP_LOGI(TAG, "Khoi tao I2C cho man hinh thu hai");

    if (!RobotSettings::ValidateOled(config)) {
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

bool SecondaryDisplay::InitializeSecondOledDisplay(const kynangso::OledConfig& config) {
    ESP_LOGI(TAG, "Khoi tao man hinh OLED thu hai %dx%d", config.width, config.height);
    if (second_display_i2c_bus_ == nullptr || !RobotSettings::ValidateOled(config)) {
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
    available_.store(true);
    ESP_LOGI(TAG, "Da khoi tao OLED mode=%d", config.mode);
    return true;
}

void SecondaryDisplay::TimeDisplayTask(void* pvParameters) {
    auto* robot = static_cast<SecondaryDisplay*>(pvParameters);

    while (!robot->stopping_.load()) {
        int delay_ms = 1000;
        if (robot->second_display_mutex_ != nullptr) {
            xSemaphoreTake(robot->second_display_mutex_, portMAX_DELAY);
            bool display_ready = robot->RenderSecondOledLocked();
            const bool temporary_text_active =
                robot->oled_text_display_active_ &&
                esp_timer_get_time() < robot->oled_text_display_until_us_;
            if (temporary_text_active ||
                robot->second_display_settings_.mode == kynangso::OledDisplayMode::Marquee) {
                int speed = std::clamp<int>(robot->second_display_settings_.speed, 1, 5);
                delay_ms = std::max(50, 260 / speed);
            } else if (robot->second_display_settings_.mode == kynangso::OledDisplayMode::Status) {
                delay_ms = 500;
            }
            xSemaphoreGive(robot->second_display_mutex_);
            if (!display_ready) {
                ESP_LOGD(TAG, "OLED thu hai chua san sang de hien thi");
            }
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
    }
    // No access to the owner after signalling: its destructor may now proceed.
    xSemaphoreGive(robot->render_task_stopped_);
    vTaskDelete(nullptr);
}

void SecondaryDisplay::ShowIp(const std::string& address) {
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

void SecondaryDisplay::ShowText(const std::string& text, int duration_ms) {
    if (second_display_mutex_ == nullptr || text.empty() || duration_ms < 1000 ||
        duration_ms > 30000) {
        return;
    }
    char normalized[sizeof(kynangso::OledDisplaySettings::text)] = {};
    kynangso::OledDisplayController::NormalizeText(text.c_str(), normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return;
    }
    xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
    std::snprintf(oled_text_display_, sizeof(oled_text_display_), "%s", normalized);
    oled_text_display_active_ = true;
    oled_text_display_until_us_ = esp_timer_get_time() + static_cast<int64_t>(duration_ms) * 1000;
    RenderSecondOledLocked();
    xSemaphoreGive(second_display_mutex_);
    ESP_LOGI(TAG, "Showing custom text on secondary OLED for %d ms", duration_ms);
}

void SecondaryDisplay::ApplySettings() {
    if (second_display_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Mutex OLED chua san sang");
        return;
    }
    xSemaphoreTake(second_display_mutex_, portMAX_DELAY);
    kynangso::OledConfig config = settings_.LoadOled();
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

void SecondaryDisplay::Reinitialize() {
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

    kynangso::OledConfig config = settings_.LoadOled();
    if (!InitializeSecondDisplayI2c(config) || !InitializeSecondOledDisplay(config)) {
        ESP_LOGE(TAG, "Khoi tao lai OLED that bai");
        if (second_display_i2c_bus_) {
            i2c_del_master_bus(second_display_i2c_bus_);
            second_display_i2c_bus_ = nullptr;
        }
    }
    xSemaphoreGive(second_display_mutex_);
}

void SecondaryDisplay::OnBootEvent(const BootEvent& event) {
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

}  // namespace kynangso
