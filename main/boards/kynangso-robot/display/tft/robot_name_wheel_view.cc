#include "display/tft/robot_name_wheel_view.h"

#include <esp_random.h>

#include <algorithm>
#include <iterator>

namespace kynangso {
namespace {
constexpr uint32_t kPalette[] = {0xA78BFA, 0x38BDF8, 0x34D399, 0xFBBF24, 0xFB7185};
constexpr uint32_t kConfettiColors[] = {0xF472B6, 0xFBBF24, 0x34D399, 0x60A5FA,
                                        0xC084FC, 0xFB7185, 0x2DD4BF, 0xF97316};
constexpr int kConfettiPositions[][2] = {{24, 54}, {198, 48}, {14, 125}, {207, 130},
                                         {37, 204}, {185, 202}, {78, 27}, {153, 30}};
}  // namespace

RobotNameWheelView::RobotNameWheelView(int width, int height, const lv_font_t* font,
                                       std::function<void()> on_finished)
    : width_(width), height_(height), on_finished_(std::move(on_finished)) {
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root_, width_, height_);
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x101126), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(root_);
    lv_label_set_text(title, "VÒNG QUAY MAY MẮN");
    lv_obj_set_width(title, width_ - 20);
    lv_obj_set_style_text_font(title, font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE9D5FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    auto* subtitle = lv_label_create(root_);
    lv_label_set_text(subtitle, "AI PICKER  ·  CHỌN NGẪU NHIÊN");
    lv_obj_set_width(subtitle, width_ - 20);
    lv_obj_set_style_text_font(subtitle, font, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x8291AA), 0);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 42);

    ring_ = lv_obj_create(root_);
    lv_obj_set_size(ring_, std::min(width_, height_) - 78, std::min(width_, height_) - 78);
    lv_obj_center(ring_);
    lv_obj_set_y(ring_, 5);
    lv_obj_set_style_bg_color(ring_, lv_color_hex(0x1B1A37), 0);
    lv_obj_set_style_bg_opa(ring_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ring_, lv_color_hex(kPalette[0]), 0);
    lv_obj_set_style_border_width(ring_, 5, 0);
    lv_obj_set_style_radius(ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_color(ring_, lv_color_hex(0x8B5CF6), 0);
    lv_obj_set_style_shadow_width(ring_, 18, 0);
    lv_obj_set_style_shadow_opa(ring_, LV_OPA_30, 0);
    lv_obj_clear_flag(ring_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(ring_);
    lv_obj_align(ring_, LV_ALIGN_CENTER, 0, 5);

    auto* pointer = lv_label_create(root_);
    lv_label_set_text(pointer, "▼");
    lv_obj_set_style_text_font(pointer, font, 0);
    lv_obj_set_style_text_color(pointer, lv_color_hex(0xFBBF24), 0);
    lv_obj_align_to(pointer, ring_, LV_ALIGN_TOP_MID, 0, -6);

    name_ = lv_label_create(root_);
    lv_obj_set_width(name_, width_ - 72);
    lv_obj_set_height(name_, 54);
    lv_label_set_long_mode(name_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(name_, font, 0);
    lv_obj_set_style_text_color(name_, lv_color_white(), 0);
    lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name_, LV_ALIGN_CENTER, 0, -7);

    result_ = lv_label_create(root_);
    lv_obj_set_width(result_, width_ - 24);
    lv_obj_set_style_text_font(result_, font, 0);
    lv_obj_set_style_text_color(result_, lv_color_hex(0xA7B4C8), 0);
    lv_obj_set_style_text_align(result_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(result_, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_label_set_text(result_, "ĐANG CHỌN...");

    for (size_t index = 0; index < std::size(confetti_); ++index) {
        confetti_[index] = lv_obj_create(root_);
        lv_obj_set_size(confetti_[index], 7, 7);
        lv_obj_set_style_bg_color(confetti_[index], lv_color_hex(kConfettiColors[index]), 0);
        lv_obj_set_style_bg_opa(confetti_[index], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(confetti_[index], 0, 0);
        lv_obj_set_style_radius(confetti_[index], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(confetti_[index], kConfettiPositions[index][0],
                       kConfettiPositions[index][1]);
        lv_obj_add_flag(confetti_[index], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

RobotNameWheelView::~RobotNameWheelView() {
    if (timer_ != nullptr)
        lv_timer_delete(timer_);
    if (root_ != nullptr)
        lv_obj_delete(root_);
}

bool RobotNameWheelView::Start(std::vector<std::string> names, std::string winner) {
    if (root_ == nullptr || names.size() < 2 || names.empty() || winner.empty())
        return false;
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    names_ = std::move(names);
    winner_ = std::move(winner);
    current_index_ = esp_random() % names_.size();
    started_at_ = lv_tick_get();
    last_change_ = 0;
    revealed_ = false;
    active_ = true;
    lv_label_set_text(name_, names_[current_index_].c_str());
    lv_label_set_text(result_, "ĐANG CHỌN...");
    for (auto* item : confetti_)
        lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
    SetVisible(true);
    timer_ = lv_timer_create(OnTimer, 40, this);
    if (timer_ == nullptr) {
        active_ = false;
        SetVisible(false);
        return false;
    }
    return true;
}

void RobotNameWheelView::SetVisible(bool visible) {
    if (root_ == nullptr)
        return;
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RobotNameWheelView::OnTimer(lv_timer_t* timer) {
    auto* view = static_cast<RobotNameWheelView*>(lv_timer_get_user_data(timer));
    if (view != nullptr && view->timer_ == timer)
        view->Tick();
}

void RobotNameWheelView::Tick() {
    const uint32_t elapsed = lv_tick_elaps(started_at_);
    if (!revealed_ && elapsed >= 5000) {
        RevealWinner();
        return;
    }
    if (revealed_ && elapsed >= 7200) {
        active_ = false;
        auto* timer = timer_;
        timer_ = nullptr;
        SetVisible(false);
        if (on_finished_)
            on_finished_();
        lv_timer_delete(timer);
        return;
    }
    if (revealed_) {
        const int burst = static_cast<int>(((elapsed - 5000) / 120) % 8);
        for (size_t index = 0; index < std::size(confetti_); ++index) {
            const int sign_x = (index % 2 == 0) ? -1 : 1;
            const int sign_y = (index < 4) ? -1 : 1;
            lv_obj_set_pos(confetti_[index], kConfettiPositions[index][0] + sign_x * burst * 3,
                           kConfettiPositions[index][1] + sign_y * burst * 3);
        }
        return;
    }

    const float progress = std::min(1.0f, static_cast<float>(elapsed) / 5000.0f);
    const uint32_t interval = 45 + static_cast<uint32_t>(progress * progress * 250.0f);
    if (elapsed - last_change_ < interval)
        return;
    last_change_ = elapsed;
    current_index_ = (current_index_ + 1 + (esp_random() % names_.size())) % names_.size();
    lv_label_set_text(name_, names_[current_index_].c_str());
    const size_t color = (elapsed / 120) % std::size(kPalette);
    lv_obj_set_style_border_color(ring_, lv_color_hex(kPalette[color]), 0);
}

void RobotNameWheelView::RevealWinner() {
    revealed_ = true;
    lv_label_set_text(name_, winner_.c_str());
    lv_obj_set_style_text_color(name_, lv_color_hex(0xFDE68A), 0);
    lv_obj_set_style_border_color(ring_, lv_color_hex(0xFBBF24), 0);
    lv_label_set_text(result_, "VÀ NGƯỜI ĐƯỢC CHỌN LÀ...");
    for (auto* item : confetti_)
        lv_obj_remove_flag(item, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace kynangso
