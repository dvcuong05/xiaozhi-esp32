#ifndef KYNANGSO_ROBOT_DISPLAY_H_
#define KYNANGSO_ROBOT_DISPLAY_H_

#include <functional>
#include "features/bluetooth/bluetooth_service.h"
#include "core/robot_settings.h"
#include "features/time/robot_time_service.h"
#include "display/lcd_display.h"
#include "display/tft/robot_eyes.h"
#include "display/tft/robot_info_shuttle_view.h"
#include "display/tft/robot_lunar_calendar_view.h"
#include "display/tft/robot_name_wheel_view.h"
#include "display/tft/robot_screen_layout.h"
#include "display/tft/robot_spectrum.h"
#include "display/tft/robot_time_view.h"
#include "features/media/robot_music_session.h"
#include "features/payment/robot_qr_code.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

class KyNangSoRobotDisplay final : public SpiLcdDisplay {
public:
    KyNangSoRobotDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, int offset_x, int offset_y, bool mirror_x,
                         bool mirror_y, bool swap_xy);
    ~KyNangSoRobotDisplay() override;

    void SetupUI() override;
    void ShowBluetooth(const kynangso::BluetoothStatus& status, size_t selected, const std::string& message);
    void HideBluetooth();
    bool IsBluetoothVisible();
    void SetTimeView(const kynangso::TimeView& view);
    void ShowOtaProgress(const char* status, int progress);
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void RemoveChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void ShowStartupHint(const char* text) override;
    void HideStartupHint(const char* text) override;
    void SetHideSubtitle(bool hide) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void ConfigureFace(const kynangso::FaceConfig& config);
    void SetFaceTheme(const kynangso::FaceTheme& theme);
    void ConfigureInfoShuttle(const kynangso::InfoShuttleConfig& config);
    void ShowNextInfoShuttleCard();
    void SetUpcomingScheduleProvider(std::function<kynangso::UpcomingScheduleList()> provider);
    void SetInfoShuttleWeather(const std::string& summary, int icon_kind = -1);
    void PreviewFaceEmotion(const char* emotion, int duration_ms = 2000);
    void SetBehaviorCallbacks(std::function<void(bool)> speaking,
                              std::function<void(const char*)> emotion);
    void SetFaceGifRequestCallback(std::function<void(const char*)> callback);
    void StartBootSequence() override;
    void FinishBootSequence() override;
    void OnBootEvent(BootStage stage, const char* detail) override;
    bool SupportsAudioVisualization() const override { return true; }
    void OnAudioPlaybackFrame(const AudioPlaybackFrame& frame) override;
    void OnAudioPlaybackStopped() override;
    void SetMediaMetadata(const MediaPlaybackMetadata& metadata) override;
    void SetMediaPlaybackProgress(uint32_t position_ms, uint32_t duration_ms) override;
    void SetMediaSubtitle(const char* text) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image, uint32_t duration_ms) override;
    void SetLiveTvActive(bool active, const std::string& title);
    bool SetLiveTvFrame(std::unique_ptr<LvglImage> image);
    uint32_t LiveTvRefreshCount() const { return live_tv_refresh_count_.load(); }
    void SetLiveTvInterruptedCallback(std::function<void(bool)> callback);
    bool CanStartLiveTv();
    bool IsLiveTvVisible();
    bool CanStartNameWheel();
    bool StartNameWheel(std::vector<std::string> names, std::string winner);
    void ShowPaymentQr(int duration_ms = 30000,
                       std::shared_ptr<const kynangso::RobotQrCode> code = nullptr,
                       const std::string& caption = {});
    void ShowLunarCalendarResult(int solar_day, int solar_month, int solar_year,
                                 const kynangso::VietnameseLunarDate& lunar,
                                 const kynangso::LunarCalendarEvents& events = {},
                                 size_t event_count = 0, bool events_loaded = false);

protected:
    void OnThemeTextFontChanged(const lv_font_t* text_font) override;

private:
    enum class BootState { Pending, Active, Finished };
    enum class UiMode {
        NormalFace,
        Bluetooth,
        BootConsole,
        PaymentQr,
        LunarCalendar,
        AudioVisualizer,
        NameWheel,
        SdImage,
        LiveTv,
        Time,
        InfoShuttle
    };
    enum class InfoShuttleCard { None = -1, Face = 0, Schedule = 1, Weather = 2 };
    std::unique_ptr<kynangso::RobotTimeView> time_view_;
    bool time_visible_ = false;
    bool time_alert_ = false;

    std::unique_ptr<kynangso::RobotScreenLayout> screen_layout_;
    std::unique_ptr<RobotEyesView> robot_eyes_;
    kynangso::FaceTheme face_theme_;
    std::string current_face_expression_ = "neutral";
    std::function<void(const char*)> face_gif_request_callback_;
    std::unique_ptr<kynangso::RobotSpectrum> spectrum_;
    kynangso::RobotMusicSession music_session_;
    bool bluetooth_touch_enabled_ = false;
    bool bluetooth_active_ = false;
    lv_obj_t* bluetooth_overlay_ = nullptr;
    lv_obj_t* bluetooth_status_ = nullptr;
    lv_obj_t* bluetooth_rows_[3] = {};
    lv_obj_t* bluetooth_names_[3] = {};
    lv_obj_t* bluetooth_count_ = nullptr;
    lv_obj_t* bluetooth_hint_ = nullptr;
    lv_obj_t* bluetooth_spinner_ = nullptr;
    lv_obj_t* boot_console_ = nullptr;
    lv_obj_t* ota_overlay_ = nullptr;
    lv_obj_t* ota_progress_ = nullptr;
    lv_obj_t* ota_label_ = nullptr;
    lv_obj_t* boot_console_label_ = nullptr;
    lv_timer_t* boot_console_timer_ = nullptr;
    lv_obj_t* payment_qr_overlay_ = nullptr;
    lv_obj_t* payment_qr_caption_ = nullptr;
    std::shared_ptr<const kynangso::RobotQrCode> payment_qr_code_;
    lv_timer_t* payment_qr_timer_ = nullptr;
    lv_timer_t* lunar_calendar_timer_ = nullptr;
    std::unique_ptr<kynangso::RobotLunarCalendarView> lunar_calendar_view_;
    bool lunar_calendar_active_ = false;
    lv_obj_t* audio_visualizer_ = nullptr;
    lv_obj_t* sd_image_overlay_ = nullptr;
    lv_obj_t* sd_image_view_ = nullptr;
    lv_timer_t* sd_image_timer_ = nullptr;
    std::unique_ptr<LvglImage> sd_image_;
    bool sd_image_active_ = false;
    lv_obj_t* live_tv_overlay_ = nullptr;
    lv_obj_t* live_tv_image_view_ = nullptr;
    lv_obj_t* live_tv_title_ = nullptr;
    std::unique_ptr<LvglImage> live_tv_frame_;
    static void LiveTvRefreshReady(lv_event_t* event);
    std::atomic<uint32_t> live_tv_refresh_count_{0};
    uint32_t live_tv_frame_sequence_ = 0;
    uint32_t live_tv_refreshed_sequence_ = 0;
    bool live_tv_active_ = false;
    std::string live_tv_title_text_;
    std::function<void(bool)> live_tv_interrupted_callback_;
    void InterruptLiveTv(bool stop_audio);
    lv_obj_t* music_title_ = nullptr;
    lv_obj_t* music_artist_ = nullptr;
    lv_obj_t* music_progress_bar_ = nullptr;
    lv_obj_t* music_progress_time_ = nullptr;
    lv_obj_t* music_lyric_ = nullptr;
    lv_obj_t* music_previous_lyric_ = nullptr;
    std::array<lv_obj_t*, 12> audio_visualizer_bars_ = {};
    // These record requests, never LVGL visibility. QR temporarily covers the
    // current boot/playback state, so expiry restores the latest mode.
    BootState boot_state_ = BootState::Pending;
    bool audio_playback_active_ = false;
    bool payment_qr_active_ = false;
    kynangso::FaceConfig face_config_;
    lv_timer_t* face_preview_timer_ = nullptr;
    kynangso::InfoShuttleConfig info_shuttle_config_;
    lv_timer_t* info_shuttle_timer_ = nullptr;
    lv_timer_t* info_shuttle_notification_timer_ = nullptr;
    std::unique_ptr<kynangso::RobotInfoShuttleView> info_shuttle_view_;
    std::unique_ptr<kynangso::RobotNameWheelView> name_wheel_view_;
    std::function<kynangso::UpcomingScheduleList()> upcoming_schedule_provider_;
    std::string info_shuttle_weather_;
    int info_shuttle_weather_icon_ = -1;
    InfoShuttleCard info_shuttle_card_ = InfoShuttleCard::None;
    bool info_shuttle_active_ = false;
    bool info_shuttle_timer_paused_ = false;
    bool conversation_active_ = false;
    std::function<void(bool)> speaking_callback_;
    std::function<void(const char*)> emotion_callback_;
    std::string boot_console_text_;
    size_t boot_console_visible_chars_ = 0;

    static void OnBootConsoleTimer(lv_timer_t* timer);
    static void OnPaymentQrTimer(lv_timer_t* timer);
    static void OnLunarCalendarTimer(lv_timer_t* timer);
    static void OnSdImageTimer(lv_timer_t* timer);
    static void OnInfoShuttleTimer(lv_timer_t* timer);
    static void OnInfoShuttleNotificationTimer(lv_timer_t* timer);
    static void DrawPaymentQr(lv_event_t* event);
    // Called with the display lock held (including LVGL timer callbacks).
    UiMode GetUiMode() const;
    UiMode GetBaseUiMode() const;
    bool CanPresentInfoShuttle(UiMode base_mode) const;
    void ApplyUiMode();
    void HidePaymentQr();
    void HideLunarCalendar();
    void AppendBootConsoleLine(const char* text);
    void HideLegacyEmoji();
    void RenderBootConsole();
    void RenderMusicInfo();
    void AdvanceInfoShuttle();
    void RenderInfoShuttleCard();
    void NameWheelFinished();
};

#endif  // KYNANGSO_ROBOT_DISPLAY_H_
