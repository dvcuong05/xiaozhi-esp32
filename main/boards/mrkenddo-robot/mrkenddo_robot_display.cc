#include "mrkenddo_robot_display.h"

#include "assets/lang_config.h"
#include "lvgl_theme.h"
#include "payment_qr_bitmap.h"

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
    if (payment_qr_timer_ != nullptr) {
        lv_timer_delete(payment_qr_timer_);
        payment_qr_timer_ = nullptr;
    }
    if (boot_console_timer_ != nullptr) {
        lv_timer_delete(boot_console_timer_);
        boot_console_timer_ = nullptr;
    }
    if (payment_qr_overlay_ != nullptr) {
        lv_obj_delete(payment_qr_overlay_);
        payment_qr_overlay_ = nullptr;
    }
    robot_eyes_.reset();
    if (boot_console_ != nullptr) {
        lv_obj_delete(boot_console_);
        boot_console_ = nullptr;
        boot_console_label_ = nullptr;
    }
}

void MrkenDDoRobotDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();
    if (!setup_ui_called_) {
        return;
    }

    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    robot_eyes_ = std::make_unique<RobotEyesView>(lv_screen_active(), width_, height_);
    robot_eyes_->SetVisible(false);
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
    SpiLcdDisplay::SetStatus(status);
    if (robot_eyes_ == nullptr || status == nullptr) {
        return;
    }

    RobotEyesMode mode;
    if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
        mode = RobotEyesMode::Listening;
    } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
        mode = RobotEyesMode::Speaking;
    } else if (std::strcmp(status, Lang::Strings::ERROR) == 0) {
        mode = RobotEyesMode::Sad;
    } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0 ||
               std::strcmp(status, Lang::Strings::CONNECTING) == 0) {
        mode = RobotEyesMode::Neutral;
    } else {
        return;
    }

    DisplayLockGuard lock(this);
    if (lock) {
        robot_eyes_->SetStatus(mode);
        HideLegacyEmoji();
    }
}

void MrkenDDoRobotDisplay::ShowNotification(const char* notification, int duration_ms) {
    SpiLcdDisplay::ShowNotification(notification, duration_ms);
    AppendBootConsoleLine(notification);
}

void MrkenDDoRobotDisplay::SetEmotion(const char* emotion) {
    if (robot_eyes_ == nullptr) {
        SpiLcdDisplay::SetEmotion(emotion);
        return;
    }

    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }

    if (!robot_eyes_->SetEmotion(emotion)) {
        robot_eyes_->SetEmotion("neutral");
    }
    HideLegacyEmoji();
    robot_eyes_->SetVisible(true);
}

void MrkenDDoRobotDisplay::StartBootSequence() {
    if (!setup_ui_called_) {
        return;
    }

    DisplayLockGuard lock(this);
    if (!lock || boot_console_active_) {
        return;
    }

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

    boot_console_text_ = "MRKENDDO ROBOT // BOOT\n";
    boot_console_visible_chars_ = 0;
    boot_console_active_ = true;
    boot_console_timer_ = lv_timer_create(OnBootConsoleTimer, 35, this);
    RenderBootConsole();
}

void MrkenDDoRobotDisplay::FinishBootSequence() {
    DisplayLockGuard lock(this);
    if (!lock || !boot_console_active_) {
        return;
    }

    boot_console_active_ = false;
    if (boot_console_timer_ != nullptr) {
        lv_timer_delete(boot_console_timer_);
        boot_console_timer_ = nullptr;
    }
    if (boot_console_ != nullptr) {
        lv_obj_add_flag(boot_console_, LV_OBJ_FLAG_HIDDEN);
    }
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
    if (stage == BootStage::Ready && robot_eyes_ != nullptr) {
        DisplayLockGuard lock(this);
        if (lock) {
            robot_eyes_->SetEmotion("robot_2");
            HideLegacyEmoji();
            robot_eyes_->SetVisible(true);
        }
    }
}

void MrkenDDoRobotDisplay::OnThemeTextFontChanged(const lv_font_t* text_font) {
    if (boot_console_label_ != nullptr) {
        lv_obj_set_style_text_font(boot_console_label_, text_font, 0);
    }
}

void MrkenDDoRobotDisplay::ShowPaymentQr(int duration_ms) {
    if (!setup_ui_called_ || payment_qr_overlay_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }

    if (payment_qr_timer_ != nullptr) {
        lv_timer_delete(payment_qr_timer_);
        payment_qr_timer_ = nullptr;
    }
    if (boot_console_ != nullptr && boot_console_active_) {
        lv_obj_add_flag(boot_console_, LV_OBJ_FLAG_HIDDEN);
    }
    if (robot_eyes_ != nullptr) {
        robot_eyes_->SetVisible(false);
    }
    lv_obj_move_foreground(payment_qr_overlay_);
    lv_obj_remove_flag(payment_qr_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(payment_qr_overlay_);

    payment_qr_timer_ = lv_timer_create(OnPaymentQrTimer, duration_ms, this);
    lv_timer_set_repeat_count(payment_qr_timer_, 1);
}

void MrkenDDoRobotDisplay::HidePaymentQr() {
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    if (payment_qr_overlay_ != nullptr) {
        lv_obj_add_flag(payment_qr_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    if (boot_console_ != nullptr && boot_console_active_) {
        lv_obj_remove_flag(boot_console_, LV_OBJ_FLAG_HIDDEN);
    } else if (robot_eyes_ != nullptr) {
        robot_eyes_->SetVisible(true);
    }
}

void MrkenDDoRobotDisplay::OnPaymentQrTimer(lv_timer_t* timer) {
    auto* display = static_cast<MrkenDDoRobotDisplay*>(lv_timer_get_user_data(timer));
    if (display == nullptr) {
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
    constexpr int kScale = 5;
    constexpr int kQuietZone = 4;
    const int qr_size = (mrkenddo::payment_qr::kModules + 2 * kQuietZone) * kScale;
    const int overlay_width = overlay_area.x2 - overlay_area.x1 + 1;
    const int overlay_height = overlay_area.y2 - overlay_area.y1 + 1;
    const int origin_x = overlay_area.x1 + std::max(0, (overlay_width - qr_size) / 2);
    const int origin_y = overlay_area.y1 + std::max(0, (overlay_height - qr_size) / 2);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = 0;

    for (int row = 0; row < mrkenddo::payment_qr::kModules; ++row) {
        int column = 0;
        while (column < mrkenddo::payment_qr::kModules) {
            while (column < mrkenddo::payment_qr::kModules &&
                   !mrkenddo::payment_qr::IsBlack(row, column)) {
                ++column;
            }
            const int start = column;
            while (column < mrkenddo::payment_qr::kModules &&
                   mrkenddo::payment_qr::IsBlack(row, column)) {
                ++column;
            }
            if (start == column) {
                continue;
            }
            lv_area_t module_area = {
                static_cast<lv_coord_t>(origin_x + (kQuietZone + start) * kScale),
                static_cast<lv_coord_t>(origin_y + (kQuietZone + row) * kScale),
                static_cast<lv_coord_t>(origin_x + (kQuietZone + column) * kScale - 1),
                static_cast<lv_coord_t>(origin_y + (kQuietZone + row + 1) * kScale - 1),
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
    if (boot_console_active_) {
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
    if (display == nullptr || !display->boot_console_active_) {
        return;
    }
    if (display->boot_console_visible_chars_ < display->boot_console_text_.size()) {
        ++display->boot_console_visible_chars_;
    }
    display->RenderBootConsole();
}

void MrkenDDoRobotDisplay::AppendBootConsoleLine(const char* text) {
    if (!boot_console_active_ || text == nullptr || text[0] == '\0') {
        return;
    }

    DisplayLockGuard lock(this);
    if (!lock || !boot_console_active_) {
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
