#ifndef MRKENDDO_ROBOT_DISPLAY_H_
#define MRKENDDO_ROBOT_DISPLAY_H_

#include "display/lcd_display.h"
#include "robot_eyes.h"

#include <memory>
#include <string>

class MrkenDDoRobotDisplay final : public SpiLcdDisplay {
public:
    MrkenDDoRobotDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, int offset_x, int offset_y, bool mirror_x,
                         bool mirror_y, bool swap_xy);
    ~MrkenDDoRobotDisplay() override;

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void StartBootSequence() override;
    void FinishBootSequence() override;
    void OnBootEvent(BootStage stage, const char* detail) override;

protected:
    void OnThemeTextFontChanged(const lv_font_t* text_font) override;

private:
    std::unique_ptr<RobotEyesView> robot_eyes_;
    lv_obj_t* boot_console_ = nullptr;
    lv_obj_t* boot_console_label_ = nullptr;
    lv_timer_t* boot_console_timer_ = nullptr;
    std::string boot_console_text_;
    size_t boot_console_visible_chars_ = 0;
    bool boot_console_active_ = false;

    static void OnBootConsoleTimer(lv_timer_t* timer);
    void AppendBootConsoleLine(const char* text);
    void HideLegacyEmoji();
    void RenderBootConsole();
};

#endif  // MRKENDDO_ROBOT_DISPLAY_H_
