#ifndef MRKENDDO_ROBOT_DISPLAY_H_
#define MRKENDDO_ROBOT_DISPLAY_H_

#include "display/lcd_display.h"
#include "robot_eyes.h"

#include <memory>
#include <string>
#include <array>

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
    void OnAudioPlaybackFrame(const uint8_t* levels, size_t count) override;
    void OnAudioPlaybackStopped() override;
    void ShowPaymentQr(int duration_ms = 30000);

protected:
    void OnThemeTextFontChanged(const lv_font_t* text_font) override;

private:
    std::unique_ptr<RobotEyesView> robot_eyes_;
    lv_obj_t* boot_console_ = nullptr;
    lv_obj_t* boot_console_label_ = nullptr;
    lv_timer_t* boot_console_timer_ = nullptr;
    lv_obj_t* payment_qr_overlay_ = nullptr;
    lv_timer_t* payment_qr_timer_ = nullptr;
    lv_obj_t* audio_visualizer_ = nullptr;
    std::array<lv_obj_t*, 12> audio_visualizer_bars_ = {};
    bool audio_visualizer_active_ = false;
    // Keep boot console/IP presentation uninterrupted by startup audio.
    bool boot_visualizer_blocked_ = true;
    std::string boot_console_text_;
    size_t boot_console_visible_chars_ = 0;
    bool boot_console_active_ = false;

    static void OnBootConsoleTimer(lv_timer_t* timer);
    static void OnPaymentQrTimer(lv_timer_t* timer);
    static void DrawPaymentQr(lv_event_t* event);
    void HidePaymentQr();
    void HideAudioVisualizer();
    void AppendBootConsoleLine(const char* text);
    void HideLegacyEmoji();
    void RenderBootConsole();
};

#endif  // MRKENDDO_ROBOT_DISPLAY_H_
