#include "robot_screen_layout.h"

#include <algorithm>
#include <string_view>

namespace kynangso {
namespace {

void StyleRegion(lv_obj_t* object) {
    lv_obj_set_style_bg_color(object, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

// Bound storage without splitting a UTF-8 code point.
std::string ClipText(const char* text) {
    const std::string_view input(text);
    constexpr size_t kMaxBytes = 1024;
    size_t end = std::min(input.size(), kMaxBytes);
    while (end > 0 && end < input.size() &&
           (static_cast<unsigned char>(input[end]) & 0xc0) == 0x80) {
        --end;
    }
    return std::string(input.substr(0, end));
}

}  // namespace

RobotScreenLayout::RobotScreenLayout(lv_obj_t* parent, int width, int height, const lv_font_t* font)
    : width_(width), height_(height) {
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, width_, height_);
    lv_obj_center(root_);
    StyleRegion(root_);
    face_region_ = lv_obj_create(root_);
    StyleRegion(face_region_);
    info_region_ = lv_obj_create(root_);
    StyleRegion(info_region_);
    info_label_ = lv_label_create(info_region_);
    lv_obj_set_style_text_color(info_label_, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_align(info_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(info_label_, 2, 0);
    // Wrap normal sentences; overflow scrolls vertically within the text slot.
    lv_label_set_long_mode(info_label_, LV_LABEL_LONG_WRAP);
    SetFont(font);
    RenderText();
    SetVisible(false);
}

RobotScreenLayout::~RobotScreenLayout() { lv_obj_delete(root_); }

void RobotScreenLayout::SetFont(const lv_font_t* font) {
    if (font == nullptr) {
        return;
    }
    lv_obj_set_style_text_font(info_label_, font, 0);
    const int info_height = std::min(height_ / 3, 3 * (static_cast<int>(font->line_height) + 2));
    face_height_ = height_ - info_height - 12;
    lv_obj_set_size(face_region_, width_, face_height_);
    lv_obj_set_pos(face_region_, 0, 0);
    lv_obj_set_pos(info_region_, 8, face_height_ + 4);
    lv_obj_set_size(info_region_, width_ - 16, info_height);
    lv_obj_set_size(info_label_, width_ - 16, LV_SIZE_CONTENT);
    RenderText();
}

void RobotScreenLayout::SetVisible(bool visible) {
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    } else {
        // A full-screen action cancels a transient hint; it must not reappear
        // when QR or media finishes before the application timer expires.
        if (startup_hint_) {
            ClearText();
        }
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RobotScreenLayout::RenderText() {
    lv_anim_delete(info_label_, nullptr);
    lv_obj_set_y(info_label_, 0);
    lv_label_set_text(info_label_, hide_subtitle_ && !startup_hint_ ? "" : text_.c_str());
    lv_obj_update_layout(info_region_);
    const int overflow = lv_obj_get_height(info_label_) - lv_obj_get_height(info_region_);
    if (overflow > 0) {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, info_label_);
        lv_anim_set_exec_cb(&animation, [](void* object, int32_t y) {
            lv_obj_set_y(static_cast<lv_obj_t*>(object), y);
        });
        lv_anim_set_values(&animation, 0, -overflow);
        lv_anim_set_duration(&animation, std::max(2500, overflow * 60));
        lv_anim_set_delay(&animation, 1500);
        lv_anim_set_playback_duration(&animation, 600);
        lv_anim_set_playback_delay(&animation, 1500);
        lv_anim_set_repeat_delay(&animation, 1500);
        lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&animation);
    }
}

void RobotScreenLayout::SetText(const char* role, const char* text) {
    if (role == nullptr || text == nullptr) {
        return;
    }
    startup_hint_ = false;
    role_ = role;
    text_ = ClipText(text);
    RenderText();
}

void RobotScreenLayout::RemoveText(const char* role, const char* text) {
    if (role != nullptr && text != nullptr && role_ == role && text_ == ClipText(text)) {
        ClearText();
    }
}

void RobotScreenLayout::ClearText() {
    startup_hint_ = false;
    role_.clear();
    text_.clear();
    RenderText();
}

void RobotScreenLayout::ShowStartupHint(const char* text) {
    if (text == nullptr) {
        return;
    }
    SetText("assistant", text);
    startup_hint_ = true;
    RenderText();
}

void RobotScreenLayout::HideStartupHint(const char* text) {
    if (startup_hint_) {
        RemoveText("assistant", text);
    }
}

void RobotScreenLayout::DismissStartupHint() {
    if (startup_hint_) {
        ClearText();
    }
}

void RobotScreenLayout::SetHideSubtitle(bool hide) {
    hide_subtitle_ = hide;
    RenderText();
}

}  // namespace kynangso
