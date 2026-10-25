#include "mrkenddo_robot_display.h"

#include "assets/lang_config.h"
#include "lvgl_theme.h"
#include "payment_qr_bitmap.h"

#include <esp_log.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

MrkenDDoRobotDisplay::MrkenDDoRobotDisplay(esp_lcd_panel_io_handle_t panel_io,
                                           esp_lcd_panel_handle_t panel, int width, int height,
                                           int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                           bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

MrkenDDoRobotDisplay::~MrkenDDoRobotDisplay() {
    if (face_preview_timer_ != nullptr)
        lv_timer_delete(face_preview_timer_);
    if (payment_qr_timer_ != nullptr) {
        lv_timer_delete(payment_qr_timer_);
        payment_qr_timer_ = nullptr;
    }
    if (sd_image_timer_ != nullptr) {
        lv_timer_delete(sd_image_timer_);
        sd_image_timer_ = nullptr;
    }
    sd_image_.reset();
    if (sd_image_overlay_ != nullptr) {
        lv_obj_delete(sd_image_overlay_);
        sd_image_overlay_ = nullptr;
        sd_image_view_ = nullptr;
    }
    if (boot_console_timer_ != nullptr) {
        lv_timer_delete(boot_console_timer_);
        boot_console_timer_ = nullptr;
    }
    if (payment_qr_overlay_ != nullptr) {
        lv_obj_delete(payment_qr_overlay_);
        payment_qr_overlay_ = nullptr;
    }
    if (audio_visualizer_ != nullptr) {
        lv_obj_delete(audio_visualizer_);
        audio_visualizer_ = nullptr;
    }
    robot_eyes_.reset();
    screen_layout_.reset();
    if (boot_console_ != nullptr) {
        lv_obj_delete(boot_console_);
        boot_console_ = nullptr;
        boot_console_label_ = nullptr;
    }
}

void MrkenDDoRobotDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    if (!lock || robot_eyes_ != nullptr) {
        return;
    }

    SpiLcdDisplay::SetupUI();
    if (!setup_ui_called_) {
        return;
    }

    screen_layout_ = std::make_unique<mrkenddo::RobotScreenLayout>(
        lv_screen_active(), width_, height_,
        static_cast<LvglTheme*>(current_theme_)->text_font()->font());
    screen_layout_->SetHideSubtitle(hide_subtitle_);
    robot_eyes_ = std::make_unique<RobotEyesView>(screen_layout_->FaceRegion(), width_,
                                                  screen_layout_->FaceHeight());
    robot_eyes_->SetVisible(face_config_.eyes);
    robot_eyes_->SetAnimation(face_config_.animation);
    robot_eyes_->SetEmotion(mrkenddo::kFaceEmotions[face_config_.idle_emotion]);
    spectrum_ = std::make_unique<mrkenddo::RobotSpectrum>();
    HideLegacyEmoji();

    // Keep the QR above robot eyes, notifications, and chat UI refreshes.
    payment_qr_overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(payment_qr_overlay_, width_, height_);
    lv_obj_center(payment_qr_overlay_);
    lv_obj_set_style_bg_color(payment_qr_overlay_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(payment_qr_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(payment_qr_overlay_, 0, 0);
    lv_obj_set_style_radius(payment_qr_overlay_, 0, 0);
    lv_obj_set_style_pad_all(payment_qr_overlay_, 0, 0);
    lv_obj_clear_flag(payment_qr_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(payment_qr_overlay_, DrawPaymentQr, LV_EVENT_DRAW_MAIN, this);
    lv_obj_add_flag(payment_qr_overlay_, LV_OBJ_FLAG_HIDDEN);
    payment_qr_caption_ = lv_label_create(payment_qr_overlay_);
    lv_obj_set_width(payment_qr_caption_, width_ - 16);
    lv_obj_set_height(payment_qr_caption_, 24);
    lv_obj_align(payment_qr_caption_, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_set_style_text_align(payment_qr_caption_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(payment_qr_caption_, lv_color_black(), 0);
    lv_obj_set_style_text_font(payment_qr_caption_,
                               static_cast<LvglTheme*>(current_theme_)->text_font()->font(), 0);
    lv_label_set_long_mode(payment_qr_caption_, LV_LABEL_LONG_DOT);
    lv_label_set_text(payment_qr_caption_, "");

    audio_visualizer_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(audio_visualizer_, width_, height_);
    lv_obj_center(audio_visualizer_);
    lv_obj_set_style_bg_color(audio_visualizer_, lv_color_hex(0x07111F), 0);
    lv_obj_set_style_bg_opa(audio_visualizer_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(audio_visualizer_, 0, 0);
    lv_obj_set_style_radius(audio_visualizer_, 0, 0);
    lv_obj_set_style_pad_all(audio_visualizer_, 0, 0);
    lv_obj_clear_flag(audio_visualizer_, LV_OBJ_FLAG_SCROLLABLE);

    constexpr int kBarGap = 4;
    const int bar_bottom = height_ / 2;
    constexpr uint32_t kBarColors[] = {
        0x7C3AED, 0x6366F1, 0x3B82F6, 0x06B6D4, 0x14B8A6, 0x22C55E,
        0x84CC16, 0xEAB308, 0xF59E0B, 0xF97316, 0xEF4444, 0xEC4899,
    };
    const int bar_count = static_cast<int>(audio_visualizer_bars_.size());
    const int bar_width = std::max(4, (width_ - kBarGap * (bar_count + 1)) / bar_count);
    for (size_t index = 0; index < audio_visualizer_bars_.size(); ++index) {
        auto* bar = lv_obj_create(audio_visualizer_);
        audio_visualizer_bars_[index] = bar;
        lv_obj_set_size(bar, bar_width, 4);
        lv_obj_set_style_bg_color(bar, lv_color_hex(kBarColors[index]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, kBarGap + index * (bar_width + kBarGap),
                     -bar_bottom);
    }

    music_title_ = lv_label_create(audio_visualizer_);
    music_artist_ = lv_label_create(audio_visualizer_);
    music_previous_lyric_ = lv_label_create(audio_visualizer_);
    music_lyric_ = lv_label_create(audio_visualizer_);
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const auto* text_font = theme->text_font()->font();
    for (auto* label :
         {music_title_, music_artist_, music_previous_lyric_, music_lyric_, payment_qr_caption_}) {
        lv_obj_set_width(label, width_ - 20);
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(label, 2, 0);
    }
    lv_obj_set_pos(music_title_, 10, 8);
    lv_obj_set_height(music_title_, 26);
    lv_label_set_long_mode(music_title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(music_title_, lv_color_hex(0xC4B5FD), 0);
    lv_obj_set_pos(music_artist_, 10, 34);
    lv_obj_set_height(music_artist_, 20);
    lv_label_set_long_mode(music_artist_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(music_artist_, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_pos(music_previous_lyric_, 10, height_ / 2 + 14);
    lv_obj_set_height(music_previous_lyric_, 32);
    lv_label_set_long_mode(music_previous_lyric_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(music_previous_lyric_, lv_color_hex(0x71849E), 0);
    lv_obj_set_pos(music_lyric_, 10, height_ / 2 + 52);
    lv_obj_set_height(music_lyric_, height_ / 2 - 58);
    lv_label_set_long_mode(music_lyric_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(music_lyric_, lv_color_hex(0xEAF2FF), 0);
    RenderMusicInfo();

    lv_obj_add_flag(audio_visualizer_, LV_OBJ_FLAG_HIDDEN);

    sd_image_overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(sd_image_overlay_, width_, height_);
    lv_obj_center(sd_image_overlay_);
    lv_obj_set_style_bg_color(sd_image_overlay_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(sd_image_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sd_image_overlay_, 0, 0);
    lv_obj_set_style_radius(sd_image_overlay_, 0, 0);
    lv_obj_set_style_pad_all(sd_image_overlay_, 0, 0);
    lv_obj_clear_flag(sd_image_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    sd_image_view_ = lv_image_create(sd_image_overlay_);
    lv_obj_center(sd_image_view_);
    lv_obj_add_flag(sd_image_overlay_, LV_OBJ_FLAG_HIDDEN);
}

MrkenDDoRobotDisplay::UiMode MrkenDDoRobotDisplay::GetUiMode() const {
    if (payment_qr_active_) {
        return UiMode::PaymentQr;
    }
    if (boot_state_ == BootState::Active) {
        return UiMode::BootConsole;
    }
    // Keep boot console/IP presentation uninterrupted by startup audio.
    if (boot_state_ == BootState::Finished &&
        (audio_playback_active_ || !music_session_.Station().empty())) {
        return UiMode::AudioVisualizer;
    }
    if (sd_image_active_) {
        return UiMode::SdImage;
    }
    return UiMode::NormalFace;
}

void MrkenDDoRobotDisplay::ApplyUiMode() {
    const UiMode mode = GetUiMode();
    const auto set_visible = [](lv_obj_t* object, bool visible) {
        if (object == nullptr) {
            return;
        }
        if (visible) {
            lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(object);
        } else {
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
    };

    if (screen_layout_ != nullptr) {
        screen_layout_->SetVisible(mode == UiMode::NormalFace);
    }
    set_visible(boot_console_, mode == UiMode::BootConsole);
    set_visible(audio_visualizer_, mode == UiMode::AudioVisualizer);
    set_visible(sd_image_overlay_, mode == UiMode::SdImage);
    set_visible(payment_qr_overlay_, mode == UiMode::PaymentQr);
    HideLegacyEmoji();
}

void MrkenDDoRobotDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    SetPreviewImage(std::move(image), 30000);
}

void MrkenDDoRobotDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image, uint32_t duration_ms) {
    DisplayLockGuard lock(this);
    if (!lock || sd_image_overlay_ == nullptr || sd_image_view_ == nullptr)
        return;
    if (!image) {
        if (sd_image_timer_ != nullptr) {
            lv_timer_delete(sd_image_timer_);
            sd_image_timer_ = nullptr;
        }
        sd_image_active_ = false;
        lv_image_set_src(sd_image_view_, nullptr);
        sd_image_.reset();
        ApplyUiMode();
        return;
    }

    const auto* descriptor = image->image_dsc();
    if (descriptor == nullptr || descriptor->header.w == 0 || descriptor->header.h == 0)
        return;
    if (sd_image_timer_ != nullptr) {
        lv_timer_delete(sd_image_timer_);
        sd_image_timer_ = nullptr;
    }
    sd_image_ = std::move(image);
    lv_image_set_src(sd_image_view_, descriptor);
    const auto scale_x = static_cast<uint32_t>(256 * width_ / descriptor->header.w);
    const auto scale_y = static_cast<uint32_t>(256 * height_ / descriptor->header.h);
    lv_image_set_scale(sd_image_view_, std::min(scale_x, scale_y));
    sd_image_active_ = true;
    ApplyUiMode();
    sd_image_timer_ = lv_timer_create(OnSdImageTimer, duration_ms, this);
    if (sd_image_timer_ != nullptr) {
        lv_timer_set_repeat_count(sd_image_timer_, 1);
    } else {
        sd_image_active_ = false;
        lv_image_set_src(sd_image_view_, nullptr);
        sd_image_.reset();
        ApplyUiMode();
    }
}

void MrkenDDoRobotDisplay::OnSdImageTimer(lv_timer_t* timer) {
    auto* display = static_cast<MrkenDDoRobotDisplay*>(lv_timer_get_user_data(timer));
    display->sd_image_timer_ = nullptr;
    display->sd_image_active_ = false;
    lv_image_set_src(display->sd_image_view_, nullptr);
    display->sd_image_.reset();
    display->ApplyUiMode();
    lv_timer_delete(timer);
}

void MrkenDDoRobotDisplay::SetBehaviorCallbacks(std::function<void(bool)> speaking,
                                                std::function<void(const char*)> emotion) {
    DisplayLockGuard lock(this);
    if (lock) {
        speaking_callback_ = std::move(speaking);
        emotion_callback_ = std::move(emotion);
    }
}

void MrkenDDoRobotDisplay::ConfigureFace(const mrkenddo::FaceConfig& config) {
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    face_config_ = config;
    if (face_preview_timer_ != nullptr) {
        lv_timer_delete(face_preview_timer_);
        face_preview_timer_ = nullptr;
    }
    if (robot_eyes_ != nullptr) {
        robot_eyes_->SetAnimation(config.animation);
        robot_eyes_->SetEmotion(mrkenddo::kFaceEmotions[config.idle_emotion]);
        robot_eyes_->SetVisible(config.eyes);
    }
}

void MrkenDDoRobotDisplay::PreviewFaceEmotion(const char* emotion, int duration_ms) {
    DisplayLockGuard lock(this);
    if (!lock || robot_eyes_ == nullptr)
        return;
    if (face_preview_timer_ != nullptr)
        lv_timer_delete(face_preview_timer_);
    robot_eyes_->SetEmotion(emotion);
    face_preview_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* display = static_cast<MrkenDDoRobotDisplay*>(lv_timer_get_user_data(timer));
            display->robot_eyes_->SetEmotion(
                mrkenddo::kFaceEmotions[display->face_config_.idle_emotion]);
            display->face_preview_timer_ = nullptr;
            lv_timer_delete(timer);
        },
        duration_ms, this);
}

void MrkenDDoRobotDisplay::HideLegacyEmoji() {
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void MrkenDDoRobotDisplay::SetStatus(const char* status) {
    if (status == nullptr) {
        return;
    }

    bool music = false;
    {
        DisplayLockGuard lock(this);
        if (!lock) {
            return;
        }
        if (screen_layout_ != nullptr && std::strcmp(status, Lang::Strings::STANDBY) != 0) {
            // Status changes dismiss only the transient hint. Speaking text
            // follows SetChatMessage/ClearChatMessages instead.
            screen_layout_->DismissStartupHint();
        }
        if (std::strcmp(status, Lang::Strings::SPEAKING) != 0) {
            music_session_.Reset();
            audio_playback_active_ = false;
            if (spectrum_ != nullptr) {
                spectrum_->Reset();
            }
            RenderMusicInfo();
            ApplyUiMode();
        }
        music = music_session_.IsActive();
        if (speaking_callback_)
            speaking_callback_(std::strcmp(status, Lang::Strings::SPEAKING) == 0 && !music);
    }
    // The wire-level state remains speaking for tenclass music. Only the
    // presentation changes; do not break the transport's TTS lifecycle.
    SpiLcdDisplay::SetStatus(music ? "Music" : status);

    RobotEyesMode mode;
    if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
        mode = RobotEyesMode::Listening;
    } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
        mode = RobotEyesMode::Speaking;
    } else if (std::strcmp(status, Lang::Strings::ERROR) == 0) {
        mode = RobotEyesMode::Sad;
    } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0 ||
               std::strcmp(status, Lang::Strings::CONNECTING) == 0 ||
               std::strcmp(status, "Radio") == 0) {
        mode = RobotEyesMode::Neutral;
    } else {
        return;
    }

    DisplayLockGuard lock(this);
    if (lock && robot_eyes_ != nullptr) {
        robot_eyes_->SetStatus(mode);
        if (mode == RobotEyesMode::Neutral || !face_config_.follow_emotion) {
            robot_eyes_->SetEmotion(mrkenddo::kFaceEmotions[face_config_.idle_emotion]);
        }
        ApplyUiMode();
    }
}

void MrkenDDoRobotDisplay::RenderMusicInfo() {
    if (music_title_ == nullptr) {
        return;
    }
    lv_label_set_text(
        music_title_,
        music_session_.IsActive()
            ? (music_session_.Title().empty() ? "MUSIC" : music_session_.Title().c_str())
            : "RADIO / MUSIC");
    std::string artist_line = music_session_.Artist();
    if (!music_session_.Station().empty() && artist_line.empty()) {
        artist_line = "LIVE RADIO";
    }
    const uint32_t duration_seconds = music_session_.DurationMs() / 1000;
    if (duration_seconds > 0) {
        char duration[16] = {};
        std::snprintf(duration, sizeof(duration), "%lu:%02lu",
                      static_cast<unsigned long>(duration_seconds / 60),
                      static_cast<unsigned long>(duration_seconds % 60));
        if (!artist_line.empty()) {
            artist_line += " | ";
        }
        artist_line += duration;
    }
    lv_label_set_text(music_artist_, artist_line.c_str());
    lv_label_set_text(music_previous_lyric_, music_session_.PreviousLine().c_str());
    lv_label_set_text(music_lyric_,
                      music_session_.CurrentLine().empty()
                          ? (music_session_.IsActive() ? "Đang chờ lời bài hát..." : "")
                          : music_session_.CurrentLine().c_str());
}

void MrkenDDoRobotDisplay::SetChatMessage(const char* role, const char* content) {
    if (role == nullptr || content == nullptr) {
        return;
    }
    bool consumed = false;
    bool music = false;
    if (std::strcmp(role, "assistant") == 0) {
        DisplayLockGuard lock(this);
        if (!lock) {
            return;
        }
        const auto update = music_session_.OnAssistantText(content);
        consumed = update != mrkenddo::RobotMusicSession::Update::None;
        music = music_session_.IsActive();
        if (update == mrkenddo::RobotMusicSession::Update::Started ||
            update == mrkenddo::RobotMusicSession::Update::Stopped) {
            audio_playback_active_ = false;
            if (spectrum_ != nullptr) {
                spectrum_->Reset();
            }
            if (robot_eyes_ != nullptr) {
                robot_eyes_->SetSpeechLevel(0);
            }
        }
        if (consumed) {
            RenderMusicInfo();
            ApplyUiMode();
        }
    }
    if (consumed) {
        SpiLcdDisplay::SetStatus(music ? "Music" : Lang::Strings::SPEAKING);
    } else {
        DisplayLockGuard lock(this);
        if (lock && screen_layout_ != nullptr) {
            screen_layout_->SetText(role, content);
        }
    }
}

void MrkenDDoRobotDisplay::RemoveChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (lock && screen_layout_ != nullptr) {
        screen_layout_->RemoveText(role, content);
    }
}

void MrkenDDoRobotDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (lock && screen_layout_ != nullptr) {
        screen_layout_->ClearText();
    }
}

void MrkenDDoRobotDisplay::ShowStartupHint(const char* text) {
    DisplayLockGuard lock(this);
    if (lock && screen_layout_ != nullptr && GetUiMode() == UiMode::NormalFace &&
        boot_state_ == BootState::Finished && text != nullptr) {
        screen_layout_->ShowStartupHint(text);
    }
}

void MrkenDDoRobotDisplay::HideStartupHint(const char* text) {
    DisplayLockGuard lock(this);
    if (lock && screen_layout_ != nullptr) {
        screen_layout_->HideStartupHint(text);
    }
}

void MrkenDDoRobotDisplay::SetHideSubtitle(bool hide) {
    SpiLcdDisplay::SetHideSubtitle(hide);
    DisplayLockGuard lock(this);
    if (lock && screen_layout_ != nullptr) {
        screen_layout_->SetHideSubtitle(hide);
    }
}

void MrkenDDoRobotDisplay::ShowNotification(const char* notification, int duration_ms) {
    SpiLcdDisplay::ShowNotification(notification, duration_ms);
    AppendBootConsoleLine(notification);
}

void MrkenDDoRobotDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }

    if (robot_eyes_ == nullptr) {
        SpiLcdDisplay::SetEmotion(emotion);
        return;
    }

    if (emotion_callback_ && emotion != nullptr)
        emotion_callback_(emotion);
    if (face_preview_timer_ == nullptr && face_config_.follow_emotion &&
        !robot_eyes_->SetEmotion(emotion)) {
        robot_eyes_->SetEmotion("neutral");
    }
    ApplyUiMode();
}

void MrkenDDoRobotDisplay::StartBootSequence() {
    DisplayLockGuard lock(this);
    if (!lock || !setup_ui_called_ || boot_state_ == BootState::Active) {
        return;
    }

    boot_state_ = BootState::Active;
    music_session_.Reset();
    RenderMusicInfo();
    audio_playback_active_ = false;
    if (boot_console_ == nullptr) {
        auto* theme = static_cast<LvglTheme*>(current_theme_);
        boot_console_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(boot_console_, LV_HOR_RES, LV_VER_RES);
        lv_obj_align(boot_console_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(boot_console_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(boot_console_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(boot_console_, 0, 0);
        lv_obj_set_style_radius(boot_console_, 0, 0);
        lv_obj_set_style_pad_all(boot_console_, 10, 0);
        lv_obj_add_flag(boot_console_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(boot_console_, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(boot_console_, LV_SCROLLBAR_MODE_OFF);

        boot_console_label_ = lv_label_create(boot_console_);
        lv_obj_set_width(boot_console_label_, LV_HOR_RES - 20);
        lv_obj_set_height(boot_console_label_, LV_SIZE_CONTENT);
        lv_obj_align(boot_console_label_, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_text_font(boot_console_label_, theme->text_font()->font(), 0);
        lv_obj_set_style_text_color(boot_console_label_, lv_color_hex(0x0ABDE3), 0);
        lv_obj_set_style_text_line_space(boot_console_label_, 2, 0);
        lv_label_set_long_mode(boot_console_label_, LV_LABEL_LONG_WRAP);
    }

    boot_console_text_ = "MRKENDDO ROBOT // BOOT\n";
    boot_console_visible_chars_ = 0;
    boot_console_timer_ = lv_timer_create(OnBootConsoleTimer, 35, this);
    RenderBootConsole();
    ApplyUiMode();
}

void MrkenDDoRobotDisplay::FinishBootSequence() {
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }

    boot_state_ = BootState::Finished;
    if (boot_console_timer_ != nullptr) {
        lv_timer_delete(boot_console_timer_);
        boot_console_timer_ = nullptr;
    }
    ApplyUiMode();
}

void MrkenDDoRobotDisplay::OnBootEvent(BootStage stage, const char* detail) {
    char line[128] = {};
    if (detail != nullptr && detail[0] != '\0') {
        std::snprintf(line, sizeof(line), "[%s] %s", BootStageName(stage), detail);
    } else {
        std::snprintf(line, sizeof(line), "[%s]", BootStageName(stage));
    }
    AppendBootConsoleLine(line);

    if (stage == BootStage::Ready || stage == BootStage::Error) {
        FinishBootSequence();
    }
    if (stage == BootStage::Ready) {
        SetEmotion("robot_2");
    }
}

void MrkenDDoRobotDisplay::OnAudioPlaybackFrame(const AudioPlaybackFrame& frame) {
    DisplayLockGuard lock(this);
    if (!lock || audio_visualizer_ == nullptr || boot_state_ != BootState::Finished) {
        return;
    }

    const bool server_music =
        frame.source == AudioPlaybackSource::Conversation && music_session_.IsActive();
    if (frame.source == AudioPlaybackSource::Conversation && !server_music) {
        audio_playback_active_ = false;
        if (robot_eyes_ != nullptr) {
            robot_eyes_->SetSpeechLevel(mrkenddo::RobotSpectrum::SpeechLevel(frame));
        }
        ApplyUiMode();
        return;
    }
    if ((!server_music && frame.source != AudioPlaybackSource::Media) || spectrum_ == nullptr ||
        !spectrum_->Analyze(frame)) {
        return;
    }
    audio_playback_active_ = true;
    ApplyUiMode();

    const auto& levels = spectrum_->Levels();
    const int bar_area_height = std::max(20, height_ / 2 - 48);
    for (size_t index = 0; index < audio_visualizer_bars_.size(); ++index) {
        if (audio_visualizer_bars_[index] == nullptr) {
            continue;
        }
        const uint8_t level = levels[index];
        const int bar_height = 4 + (bar_area_height * level) / 255;
        lv_obj_set_height(audio_visualizer_bars_[index], bar_height);
    }
    lv_obj_invalidate(audio_visualizer_);
}

void MrkenDDoRobotDisplay::OnAudioPlaybackStopped() {
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }

    audio_playback_active_ = false;
    if (spectrum_ != nullptr) {
        spectrum_->Reset();
    }
    if (robot_eyes_ != nullptr) {
        robot_eyes_->SetSpeechLevel(0);
    }
    ApplyUiMode();
}

void MrkenDDoRobotDisplay::SetMediaMetadata(const MediaPlaybackMetadata& metadata) {
    std::string artist_line = metadata.artist;
    if (!metadata.station.empty()) {
        auto append_detail = [&artist_line](const std::string& value) {
            if (value.empty()) {
                return;
            }
            if (!artist_line.empty()) {
                artist_line += " | ";
            }
            artist_line += value;
        };
        append_detail(metadata.frequency);
        append_detail(metadata.genre);
        append_detail(metadata.country);
        if (metadata.bitrate_kbps > 0 || !metadata.codec.empty()) {
            std::string stream_quality;
            if (metadata.bitrate_kbps > 0) {
                stream_quality = std::to_string(metadata.bitrate_kbps) + " kbps";
            }
            if (!metadata.codec.empty()) {
                if (!stream_quality.empty()) {
                    stream_quality += " ";
                }
                stream_quality += metadata.codec;
            }
            append_detail(stream_quality);
        }
        if (artist_line.empty()) {
            artist_line = "LIVE RADIO";
        }
    }
    {
        DisplayLockGuard lock(this);
        if (!lock) {
            return;
        }
        if (speaking_callback_)
            speaking_callback_(false);
        music_session_.SetMediaMetadata(metadata.title, artist_line, metadata.station,
                                        metadata.duration_ms);
        audio_playback_active_ = false;
        if (spectrum_ != nullptr) {
            spectrum_->Reset();
        }
        RenderMusicInfo();
        ApplyUiMode();
        if (!metadata.station.empty()) {
            ESP_LOGI("RobotDisplay", "Radio TFT: station=%s details=%s visible=%d",
                     music_session_.Title().c_str(), music_session_.Artist().c_str(),
                     audio_visualizer_ != nullptr &&
                         !lv_obj_has_flag(audio_visualizer_, LV_OBJ_FLAG_HIDDEN));
        }
    }
    if (!metadata.title.empty() || !metadata.station.empty()) {
        SpiLcdDisplay::SetStatus(metadata.station.empty() ? "Music" : "Radio");
    }
}

void MrkenDDoRobotDisplay::SetMediaSubtitle(const char* text) {
    if (text == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    music_session_.SetSubtitle(text);
    RenderMusicInfo();
}

void MrkenDDoRobotDisplay::OnThemeTextFontChanged(const lv_font_t* text_font) {
    if (screen_layout_ != nullptr) {
        screen_layout_->SetFont(text_font);
        if (robot_eyes_ != nullptr) {
            robot_eyes_->SetSize(width_, screen_layout_->FaceHeight());
        }
    }
    if (boot_console_label_ != nullptr) {
        lv_obj_set_style_text_font(boot_console_label_, text_font, 0);
    }
    for (auto* label :
         {music_title_, music_artist_, music_previous_lyric_, music_lyric_, payment_qr_caption_}) {
        if (label != nullptr) {
            lv_obj_set_style_text_font(label, text_font, 0);
        }
    }
}

void MrkenDDoRobotDisplay::ShowPaymentQr(int duration_ms,
                                         std::shared_ptr<const mrkenddo::RobotQrCode> code,
                                         const std::string& caption) {
    DisplayLockGuard lock(this);
    if (!lock || !setup_ui_called_ || payment_qr_overlay_ == nullptr) {
        return;
    }

    if (payment_qr_timer_ != nullptr) {
        lv_timer_delete(payment_qr_timer_);
        payment_qr_timer_ = nullptr;
    }
    payment_qr_code_ = std::move(code);
    lv_label_set_text(payment_qr_caption_, caption.c_str());
    payment_qr_active_ = true;
    ApplyUiMode();
    lv_obj_invalidate(payment_qr_overlay_);

    payment_qr_timer_ = lv_timer_create(OnPaymentQrTimer, std::max(1, duration_ms), this);
    lv_timer_set_repeat_count(payment_qr_timer_, 1);
}

void MrkenDDoRobotDisplay::HidePaymentQr() {
    payment_qr_active_ = false;
    payment_qr_code_.reset();
    ApplyUiMode();
}

void MrkenDDoRobotDisplay::OnPaymentQrTimer(lv_timer_t* timer) {
    auto* display = static_cast<MrkenDDoRobotDisplay*>(lv_timer_get_user_data(timer));
    if (display == nullptr || display->payment_qr_timer_ != timer) {
        return;
    }
    display->payment_qr_timer_ = nullptr;
    display->HidePaymentQr();
}

void MrkenDDoRobotDisplay::DrawPaymentQr(lv_event_t* event) {
    auto* display = static_cast<MrkenDDoRobotDisplay*>(lv_event_get_user_data(event));
    if (display == nullptr || display->payment_qr_overlay_ == nullptr) {
        return;
    }

    lv_layer_t* layer = lv_event_get_layer(event);
    if (layer == nullptr) {
        return;
    }

    lv_area_t overlay_area;
    lv_obj_get_coords(display->payment_qr_overlay_, &overlay_area);
    constexpr int kQuietZone = 4;
    const int overlay_width = overlay_area.x2 - overlay_area.x1 + 1;
    const bool has_caption = lv_label_get_text(display->payment_qr_caption_)[0] != '\0';
    const int overlay_height = overlay_area.y2 - overlay_area.y1 + 1 - (has_caption ? 28 : 0);
    const auto* code = display->payment_qr_code_.get();
    const int modules = code != nullptr ? code->modules : mrkenddo::payment_qr::kModules;
    const int scale = std::min(overlay_width, overlay_height) / (modules + 2 * kQuietZone);
    if (scale < 2)
        return;
    const int qr_size = (modules + 2 * kQuietZone) * scale;
    const auto is_black = [code](int row, int column) {
        return code != nullptr ? code->IsBlack(row, column)
                               : mrkenddo::payment_qr::IsBlack(row, column);
    };
    const int origin_x = overlay_area.x1 + std::max(0, (overlay_width - qr_size) / 2);
    const int origin_y = overlay_area.y1 + std::max(0, (overlay_height - qr_size) / 2);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = 0;

    for (int row = 0; row < modules; ++row) {
        int column = 0;
        while (column < modules) {
            while (column < modules && !is_black(row, column)) {
                ++column;
            }
            const int start = column;
            while (column < modules && is_black(row, column)) {
                ++column;
            }
            if (start == column) {
                continue;
            }
            lv_area_t module_area = {
                static_cast<lv_coord_t>(origin_x + (kQuietZone + start) * scale),
                static_cast<lv_coord_t>(origin_y + (kQuietZone + row) * scale),
                static_cast<lv_coord_t>(origin_x + (kQuietZone + column) * scale - 1),
                static_cast<lv_coord_t>(origin_y + (kQuietZone + row + 1) * scale - 1),
            };
            lv_draw_rect(layer, &rect_dsc, &module_area);
        }
    }
}

void MrkenDDoRobotDisplay::RenderBootConsole() {
    if (boot_console_label_ == nullptr) {
        return;
    }

    const size_t visible = std::min(boot_console_visible_chars_, boot_console_text_.size());
    std::string text = boot_console_text_.substr(0, visible);
    if (boot_state_ == BootState::Active) {
        text += ((visible / 8) % 2 == 0) ? "_" : " ";
    }
    lv_label_set_text(boot_console_label_, text.c_str());
    lv_obj_update_layout(boot_console_);
    const int32_t bottom =
        lv_obj_get_scroll_y(boot_console_) + lv_obj_get_scroll_bottom(boot_console_);
    lv_obj_scroll_to_y(boot_console_, bottom, LV_ANIM_OFF);
}

void MrkenDDoRobotDisplay::OnBootConsoleTimer(lv_timer_t* timer) {
    auto* display = static_cast<MrkenDDoRobotDisplay*>(lv_timer_get_user_data(timer));
    if (display == nullptr || display->boot_state_ != BootState::Active) {
        return;
    }
    if (display->boot_console_visible_chars_ < display->boot_console_text_.size()) {
        ++display->boot_console_visible_chars_;
    }
    display->RenderBootConsole();
}

void MrkenDDoRobotDisplay::AppendBootConsoleLine(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    DisplayLockGuard lock(this);
    if (!lock || boot_state_ != BootState::Active) {
        return;
    }

    if (!boot_console_text_.empty() && boot_console_text_.back() != '\n') {
        boot_console_text_ += '\n';
    }
    boot_console_text_ += text;
    boot_console_text_ += '\n';

    constexpr size_t kMaxBootConsoleChars = 1200;
    if (boot_console_text_.size() > kMaxBootConsoleChars) {
        const size_t trim = boot_console_text_.size() - kMaxBootConsoleChars;
        const size_t next_line = boot_console_text_.find('\n', trim);
        boot_console_text_.erase(0, next_line == std::string::npos ? trim : next_line + 1);
        boot_console_visible_chars_ =
            std::min(boot_console_visible_chars_, boot_console_text_.size());
    }
    RenderBootConsole();
}
