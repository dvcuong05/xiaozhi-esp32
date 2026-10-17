#ifndef MRKENDDO_ROBOT_DISPLAY_H_
#define MRKENDDO_ROBOT_DISPLAY_H_

#include "display/lcd_display.h"
#include "robot_eyes.h"
#include "robot_music_session.h"
#include "robot_qr_code.h"
#include "robot_spectrum.h"

#include <array>
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
    void SetChatMessage(const char* role, const char* content) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void StartBootSequence() override;
    void FinishBootSequence() override;
    void OnBootEvent(BootStage stage, const char* detail) override;
    bool SupportsAudioVisualization() const override { return true; }
    void OnAudioPlaybackFrame(const AudioPlaybackFrame& frame) override;
    void OnAudioPlaybackStopped() override;
    void ShowPaymentQr(int duration_ms = 30000,
                       std::shared_ptr<const mrkenddo::RobotQrCode> code = nullptr,
                       const std::string& caption = {});

protected:
    void OnThemeTextFontChanged(const lv_font_t* text_font) override;

private:
    enum class BootState { Pending, Active, Finished };
    enum class UiMode { Eyes, BootConsole, PaymentQr, AudioVisualizer };

    std::unique_ptr<RobotEyesView> robot_eyes_;
    std::unique_ptr<mrkenddo::RobotSpectrum> spectrum_;
    mrkenddo::RobotMusicSession music_session_;
    lv_obj_t* boot_console_ = nullptr;
    lv_obj_t* boot_console_label_ = nullptr;
    lv_timer_t* boot_console_timer_ = nullptr;
    lv_obj_t* payment_qr_overlay_ = nullptr;
    lv_obj_t* payment_qr_caption_ = nullptr;
    std::shared_ptr<const mrkenddo::RobotQrCode> payment_qr_code_;
    lv_timer_t* payment_qr_timer_ = nullptr;
    lv_obj_t* audio_visualizer_ = nullptr;
    lv_obj_t* music_title_ = nullptr;
    lv_obj_t* music_lyric_ = nullptr;
    lv_obj_t* music_previous_lyric_ = nullptr;
    std::array<lv_obj_t*, 12> audio_visualizer_bars_ = {};
    // These record requests, never LVGL visibility. QR temporarily covers the
    // current boot/playback state, so expiry restores the latest mode.
    BootState boot_state_ = BootState::Pending;
    bool audio_playback_active_ = false;
    bool payment_qr_active_ = false;
    std::string boot_console_text_;
    size_t boot_console_visible_chars_ = 0;

    static void OnBootConsoleTimer(lv_timer_t* timer);
    static void OnPaymentQrTimer(lv_timer_t* timer);
    static void DrawPaymentQr(lv_event_t* event);
    // Called with the display lock held (including LVGL timer callbacks).
    UiMode GetUiMode() const;
    void ApplyUiMode();
    void HidePaymentQr();
    void AppendBootConsoleLine(const char* text);
    void HideLegacyEmoji();
    void RenderBootConsole();
    void RenderMusicInfo();
};

#endif  // MRKENDDO_ROBOT_DISPLAY_H_
