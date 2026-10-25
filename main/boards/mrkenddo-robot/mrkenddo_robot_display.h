#ifndef MRKENDDO_ROBOT_DISPLAY_H_
#define MRKENDDO_ROBOT_DISPLAY_H_

#include <functional>
#include "display/lcd_display.h"
#include "robot_eyes.h"
#include "robot_music_session.h"
#include "robot_qr_code.h"
#include "robot_screen_layout.h"
#include "robot_settings.h"
#include "robot_spectrum.h"
#include "robot_time_view.h"

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
    void SetTimeView(const mrkenddo::TimeView& view);
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void RemoveChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void ShowStartupHint(const char* text) override;
    void HideStartupHint(const char* text) override;
    void SetHideSubtitle(bool hide) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void ConfigureFace(const mrkenddo::FaceConfig& config);
    void PreviewFaceEmotion(const char* emotion, int duration_ms = 2000);
    void SetBehaviorCallbacks(std::function<void(bool)> speaking,
                              std::function<void(const char*)> emotion);
    void StartBootSequence() override;
    void FinishBootSequence() override;
    void OnBootEvent(BootStage stage, const char* detail) override;
    bool SupportsAudioVisualization() const override { return true; }
    void OnAudioPlaybackFrame(const AudioPlaybackFrame& frame) override;
    void OnAudioPlaybackStopped() override;
    void SetMediaMetadata(const MediaPlaybackMetadata& metadata) override;
    void SetMediaSubtitle(const char* text) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image, uint32_t duration_ms) override;
    void ShowPaymentQr(int duration_ms = 30000,
                       std::shared_ptr<const mrkenddo::RobotQrCode> code = nullptr,
                       const std::string& caption = {});

protected:
    void OnThemeTextFontChanged(const lv_font_t* text_font) override;

private:
    enum class BootState { Pending, Active, Finished };
    enum class UiMode { NormalFace, BootConsole, PaymentQr, AudioVisualizer, SdImage, Time };
    std::unique_ptr<mrkenddo::RobotTimeView> time_view_;
    bool time_visible_ = false;
    bool time_alert_ = false;

    std::unique_ptr<mrkenddo::RobotScreenLayout> screen_layout_;
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
    lv_obj_t* sd_image_overlay_ = nullptr;
    lv_obj_t* sd_image_view_ = nullptr;
    lv_timer_t* sd_image_timer_ = nullptr;
    std::unique_ptr<LvglImage> sd_image_;
    bool sd_image_active_ = false;
    lv_obj_t* music_title_ = nullptr;
    lv_obj_t* music_artist_ = nullptr;
    lv_obj_t* music_lyric_ = nullptr;
    lv_obj_t* music_previous_lyric_ = nullptr;
    std::array<lv_obj_t*, 12> audio_visualizer_bars_ = {};
    // These record requests, never LVGL visibility. QR temporarily covers the
    // current boot/playback state, so expiry restores the latest mode.
    BootState boot_state_ = BootState::Pending;
    bool audio_playback_active_ = false;
    bool payment_qr_active_ = false;
    mrkenddo::FaceConfig face_config_;
    lv_timer_t* face_preview_timer_ = nullptr;
    std::function<void(bool)> speaking_callback_;
    std::function<void(const char*)> emotion_callback_;
    std::string boot_console_text_;
    size_t boot_console_visible_chars_ = 0;

    static void OnBootConsoleTimer(lv_timer_t* timer);
    static void OnPaymentQrTimer(lv_timer_t* timer);
    static void OnSdImageTimer(lv_timer_t* timer);
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
