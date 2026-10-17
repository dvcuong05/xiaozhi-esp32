#ifndef MRKENDDO_ROBOT_SECONDARY_DISPLAY_H_
#define MRKENDDO_ROBOT_SECONDARY_DISPLAY_H_

#include "boot_sequence.h"
#include "oled_display_controller.h"

#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <string>

namespace mrkenddo {
class RobotSettings;
struct OledConfig;

// Public operations run on the application task; the render task shares the mutex.
class SecondaryDisplay {
public:
    explicit SecondaryDisplay(RobotSettings& settings);
    ~SecondaryDisplay();

    SecondaryDisplay(const SecondaryDisplay&) = delete;
    SecondaryDisplay& operator=(const SecondaryDisplay&) = delete;

    void Initialize();
    void Reinitialize();
    void ApplySettings();
    bool IsAvailable() const { return available_.load(); }
    void ShowIp(const std::string& address);
    void ShowText(const std::string& text, int duration_ms);
    void OnBootEvent(const BootEvent& event);

private:
    RobotSettings& settings_;
    std::atomic<bool> available_{false};
    std::atomic<bool> stopping_{false};
    SemaphoreHandle_t render_task_stopped_ = nullptr;
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

    void DestroySecondDisplayPanel();
    void SetActiveOledConfig(const mrkenddo::OledConfig& config);
    esp_err_t ApplyOledPanelSettings();
    const char* GetOledDeviceStateLabel() const;
    bool RenderSecondOledLocked();
    bool InitializeSecondDisplayI2c(const mrkenddo::OledConfig& config);
    bool InitializeSecondOledDisplay(const mrkenddo::OledConfig& config);
    static void TimeDisplayTask(void* context);
};

}  // namespace mrkenddo

#endif  // MRKENDDO_ROBOT_SECONDARY_DISPLAY_H_
