#include "display/tft/robot_name_wheel_view.h"

#include <esp_random.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

LV_FONT_DECLARE(robot_time_font_14);
LV_FONT_DECLARE(robot_info_bold_20);

namespace kynangso {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr uint32_t kSpinDurationMs = 5000;
constexpr uint32_t kResultHoldMs = 10000;
constexpr uint32_t kFireworkCycleMs = 2500;
constexpr uint32_t kFireworkBurstMs = 1000;

constexpr uint32_t kSliceColors[] = {0x6D5DFB, 0x087E8B, 0x27864B, 0xC05B31,
                                     0xC24168, 0x2563EB, 0x7C3AED, 0x0F766E};
constexpr uint32_t kFireworkColors[] = {0xF472B6, 0xFBBF24, 0x34D399, 0x60A5FA, 0xC084FC, 0xFB7185,
                                        0x2DD4BF, 0xF97316, 0xFDE047, 0x38BDF8, 0xA3E635, 0xE879F9,
                                        0xFDBA74, 0x4ADE80, 0x818CF8, 0xFDA4AF};
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

    wheel_diameter_ = std::max(80, std::min(width_, height_) - 82);
    wheel_ = lv_obj_create(root_);
    lv_obj_remove_style_all(wheel_);
    lv_obj_set_size(wheel_, wheel_diameter_, wheel_diameter_);
    lv_obj_align(wheel_, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_bg_opa(wheel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(wheel_, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_border_width(wheel_, 3, 0);
    lv_obj_set_style_radius(wheel_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_color(wheel_, lv_color_hex(0x8B5CF6), 0);
    lv_obj_set_style_shadow_width(wheel_, 12, 0);
    lv_obj_set_style_shadow_opa(wheel_, LV_OPA_40, 0);
    lv_obj_remove_flag(wheel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(wheel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wheel_, DrawWheel, LV_EVENT_DRAW_MAIN, this);

    title_ = lv_label_create(root_);
    lv_obj_set_size(title_, width_ - 16, 26);
    lv_label_set_text(title_, "VÒNG QUAY MAY MẮN");
    lv_obj_set_style_text_font(title_, &robot_info_bold_20, 0);
    lv_obj_set_style_text_color(title_, lv_color_hex(0xF3E8FF), 0);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 7);

    pointer_ = lv_obj_create(root_);
    lv_obj_remove_style_all(pointer_);
    lv_obj_set_size(pointer_, 20, 14);
    lv_obj_set_style_bg_opa(pointer_, LV_OPA_TRANSP, 0);
    lv_obj_align_to(pointer_, wheel_, LV_ALIGN_TOP_MID, 0, -7);
    lv_obj_remove_flag(pointer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(pointer_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pointer_, DrawPointer, LV_EVENT_DRAW_MAIN, this);

    result_ = lv_label_create(root_);
    lv_obj_set_size(result_, width_ - 20, 20);
    lv_label_set_long_mode(result_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(result_, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_align(result_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(result_, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_label_set_text(result_, "ĐANG QUAY...");
    SetFont(font);

    for (size_t index = 0; index < std::size(fireworks_); ++index) {
        auto* particle = lv_obj_create(root_);
        const int size = index % 3 == 0 ? 5 : 4;
        lv_obj_set_size(particle, size, size + 4);
        lv_obj_set_style_bg_color(particle, lv_color_hex(kFireworkColors[index]), 0);
        lv_obj_set_style_bg_opa(particle, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(particle, 0, 0);
        lv_obj_set_style_radius(particle, LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(particle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(particle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(particle, LV_OBJ_FLAG_HIDDEN);
        fireworks_[index] = particle;
    }

    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

RobotNameWheelView::~RobotNameWheelView() {
    if (timer_ != nullptr)
        lv_timer_delete(timer_);
    if (root_ != nullptr)
        lv_obj_delete(root_);
}

void RobotNameWheelView::SetFont(const lv_font_t* font) {
    fallback_font_ = font;
    text_font_ = robot_time_font_14;
    text_font_.fallback = fallback_font_;
    if (title_ != nullptr)
        lv_obj_set_style_text_font(title_, &robot_info_bold_20, 0);
    if (result_ != nullptr)
        lv_obj_set_style_text_font(result_, &text_font_, 0);
    for (auto* label : slice_labels_) {
        if (label != nullptr)
            lv_obj_set_style_text_font(label, &text_font_, 0);
    }
}

bool RobotNameWheelView::Start(std::vector<std::string> names, std::string winner) {
    if (root_ == nullptr || names.size() < 2 || names.size() > 16 || winner.empty())
        return false;
    const auto winner_it = std::find(names.begin(), names.end(), winner);
    if (winner_it == names.end())
        return false;
    const size_t winner_index = static_cast<size_t>(std::distance(names.begin(), winner_it));
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    for (auto* label : slice_labels_)
        lv_obj_delete(label);
    slice_labels_.clear();

    names_ = std::move(names);
    winner_ = std::move(winner);
    winner_index_ = winner_index;
    for (const auto& name : names_) {
        auto* label = lv_label_create(root_);
        lv_label_set_text(label, name.c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, &text_font_, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(label, 0, 0);
        slice_labels_.push_back(label);
    }

    const float random_turn =
        static_cast<float>(esp_random()) / static_cast<float>(UINT32_MAX) * kTwoPi;
    wheel_rotation_ = random_turn;
    spin_start_rotation_ = wheel_rotation_;
    const float slice_angle = kTwoPi / static_cast<float>(names_.size());
    const float target_angle =
        -kPi / 2.0f - (static_cast<float>(winner_index_) + 0.5f) * slice_angle;
    float extra_turn = std::fmod(target_angle - wheel_rotation_, kTwoPi);
    if (extra_turn < 0.0f)
        extra_turn += kTwoPi;
    spin_target_rotation_ = wheel_rotation_ + 5.0f * kTwoPi + extra_turn;

    started_at_ = lv_tick_get();
    revealed_ = false;
    active_ = true;
    lv_label_set_text(result_, "ĐANG QUAY...");
    lv_obj_set_style_text_color(result_, lv_color_hex(0xCBD5E1), 0);
    for (auto* particle : fireworks_)
        lv_obj_add_flag(particle, LV_OBJ_FLAG_HIDDEN);
    UpdateSliceLabels();
    lv_obj_invalidate(wheel_);
    lv_obj_invalidate(pointer_);
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

void RobotNameWheelView::DrawWheel(lv_event_t* event) {
    auto* view = static_cast<RobotNameWheelView*>(lv_event_get_user_data(event));
    if (view == nullptr || view->names_.size() < 2)
        return;

    lv_area_t bounds;
    lv_obj_get_coords(view->wheel_, &bounds);
    lv_layer_t* layer = lv_event_get_layer(event);
    const int center_x = (bounds.x1 + bounds.x2) / 2;
    const int center_y = (bounds.y1 + bounds.y2) / 2;
    const float radius = static_cast<float>(view->wheel_diameter_) / 2.0f - 5.0f;
    const float slice_angle = kTwoPi / static_cast<float>(view->names_.size());
    const int steps_per_slice =
        std::max(2, static_cast<int>(std::ceil(slice_angle / (kPi / 24.0f))));

    for (size_t index = 0; index < view->names_.size(); ++index) {
        const uint32_t color = view->revealed_ && index == view->winner_index_
                                   ? 0xFBBF24
                                   : kSliceColors[index % std::size(kSliceColors)];
        const float start =
            view->wheel_rotation_ - kPi / 2.0f + static_cast<float>(index) * slice_angle;
        for (int step = 0; step < steps_per_slice; ++step) {
            const float angle1 = start + slice_angle * static_cast<float>(step) /
                                             static_cast<float>(steps_per_slice);
            const float angle2 = start + slice_angle * static_cast<float>(step + 1) /
                                             static_cast<float>(steps_per_slice);
            lv_draw_triangle_dsc_t triangle;
            lv_draw_triangle_dsc_init(&triangle);
            triangle.p[0] = {center_x, center_y};
            triangle.p[1] = {center_x + static_cast<int>(std::cos(angle1) * radius),
                             center_y + static_cast<int>(std::sin(angle1) * radius)};
            triangle.p[2] = {center_x + static_cast<int>(std::cos(angle2) * radius),
                             center_y + static_cast<int>(std::sin(angle2) * radius)};
            triangle.color = lv_color_hex(color);
            triangle.opa = LV_OPA_COVER;
            lv_draw_triangle(layer, &triangle);
        }

        lv_draw_line_dsc_t divider;
        lv_draw_line_dsc_init(&divider);
        divider.p1.x = center_x;
        divider.p1.y = center_y;
        divider.p2.x = center_x + static_cast<int>(std::cos(start) * radius);
        divider.p2.y = center_y + static_cast<int>(std::sin(start) * radius);
        divider.width = 2;
        divider.color = lv_color_hex(0x172033);
        divider.round_start = divider.round_end = true;
        lv_draw_line(layer, &divider);
    }

    lv_draw_rect_dsc_t hub;
    lv_draw_rect_dsc_init(&hub);
    hub.bg_color = lv_color_hex(0x111827);
    hub.bg_opa = LV_OPA_COVER;
    hub.border_color = lv_color_hex(0xF8FAFC);
    hub.border_width = 2;
    hub.radius = LV_RADIUS_CIRCLE;
    const lv_area_t hub_area = {center_x - 8, center_y - 8, center_x + 8, center_y + 8};
    lv_draw_rect(layer, &hub, &hub_area);
}

void RobotNameWheelView::DrawPointer(lv_event_t* event) {
    auto* view = static_cast<RobotNameWheelView*>(lv_event_get_user_data(event));
    if (view == nullptr)
        return;
    lv_area_t bounds;
    lv_obj_get_coords(view->pointer_, &bounds);
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.p[0] = {bounds.x1, bounds.y1};
    triangle.p[1] = {bounds.x2, bounds.y1};
    triangle.p[2] = {(bounds.x1 + bounds.x2) / 2, bounds.y2};
    triangle.color = lv_color_hex(view->revealed_ ? 0xFBBF24 : 0xF8FAFC);
    triangle.opa = LV_OPA_COVER;
    lv_draw_triangle(lv_event_get_layer(event), &triangle);
}

void RobotNameWheelView::UpdateSliceLabels() {
    if (slice_labels_.empty())
        return;
    lv_area_t root_bounds;
    lv_area_t wheel_bounds;
    lv_obj_get_coords(root_, &root_bounds);
    lv_obj_get_coords(wheel_, &wheel_bounds);
    const float center_x = (wheel_bounds.x1 + wheel_bounds.x2) / 2.0f - root_bounds.x1;
    const float center_y = (wheel_bounds.y1 + wheel_bounds.y2) / 2.0f - root_bounds.y1;
    const float radius = static_cast<float>(wheel_diameter_) * 0.29f;
    const float slice_angle = kTwoPi / static_cast<float>(slice_labels_.size());
    const float chord_width = 2.0f * radius * std::sin(slice_angle * 0.48f) - 4.0f;
    const int max_width = std::max(24, width_ - 24);
    const int label_width = std::clamp(static_cast<int>(chord_width), 24, max_width);

    for (size_t index = 0; index < slice_labels_.size(); ++index) {
        auto* label = slice_labels_[index];
        const float angle =
            wheel_rotation_ - kPi / 2.0f + (static_cast<float>(index) + 0.5f) * slice_angle;
        const int x = static_cast<int>(std::lround(center_x + std::cos(angle) * radius -
                                                   static_cast<float>(label_width) / 2.0f));
        const int y = static_cast<int>(std::lround(center_y + std::sin(angle) * radius - 9.0f));
        lv_obj_set_size(label, label_width, 18);
        lv_obj_set_pos(label, x, y);
        lv_obj_set_style_text_color(
            label, revealed_ && index == winner_index_ ? lv_color_hex(0x111827) : lv_color_white(),
            0);
    }
}

void RobotNameWheelView::UpdateFireworks(uint32_t elapsed) {
    if (!revealed_ || elapsed < kSpinDurationMs)
        return;
    const uint32_t celebration_elapsed = elapsed - kSpinDurationMs;
    for (size_t index = 0; index < std::size(fireworks_); ++index) {
        const size_t group = index / 8;
        const size_t ray = index % 8;
        const uint32_t phase =
            (celebration_elapsed + group * (kFireworkCycleMs / 2)) % kFireworkCycleMs;
        auto* particle = fireworks_[index];
        if (phase >= kFireworkBurstMs) {
            lv_obj_add_flag(particle, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_remove_flag(particle, LV_OBJ_FLAG_HIDDEN);
        const float progress = static_cast<float>(phase) / static_cast<float>(kFireworkBurstMs);
        const float angle =
            static_cast<float>(ray) * kPi / 4.0f + static_cast<float>(group) * kPi / 8.0f;
        const float distance = 8.0f + progress * 39.0f;
        const float origin_x = static_cast<float>(width_) * (group == 0 ? 0.20f : 0.80f);
        const float origin_y = static_cast<float>(height_) * 0.43f;
        const int x = static_cast<int>(std::lround(origin_x + std::cos(angle) * distance));
        const int y = static_cast<int>(std::lround(origin_y + std::sin(angle) * distance));
        lv_obj_set_pos(particle, x, y);
        lv_obj_set_style_bg_opa(particle, static_cast<lv_opa_t>(255.0f * (1.0f - progress)), 0);
        lv_obj_set_style_transform_angle(particle, static_cast<int>(angle * 1800.0f / kPi), 0);
    }
}

void RobotNameWheelView::OnTimer(lv_timer_t* timer) {
    auto* view = static_cast<RobotNameWheelView*>(lv_timer_get_user_data(timer));
    if (view != nullptr && view->timer_ == timer)
        view->Tick();
}

void RobotNameWheelView::Tick() {
    const uint32_t elapsed = lv_tick_elaps(started_at_);
    if (!revealed_) {
        if (elapsed >= kSpinDurationMs) {
            wheel_rotation_ = spin_target_rotation_;
            UpdateSliceLabels();
            RevealWinner();
            return;
        }
        const float progress = static_cast<float>(elapsed) / static_cast<float>(kSpinDurationMs);
        const float eased_progress = 1.0f - std::pow(1.0f - progress, 3.0f);
        wheel_rotation_ =
            spin_start_rotation_ + (spin_target_rotation_ - spin_start_rotation_) * eased_progress;
        UpdateSliceLabels();
        lv_obj_invalidate(wheel_);
        return;
    }

    UpdateFireworks(elapsed);
    if (elapsed >= kSpinDurationMs + kResultHoldMs) {
        active_ = false;
        auto* timer = timer_;
        timer_ = nullptr;
        SetVisible(false);
        if (on_finished_)
            on_finished_();
        lv_timer_delete(timer);
    }
}

void RobotNameWheelView::RevealWinner() {
    revealed_ = true;
    lv_label_set_text(result_, ("KẾT QUẢ: " + winner_).c_str());
    lv_obj_set_style_text_color(result_, lv_color_hex(0xFDE68A), 0);
    UpdateSliceLabels();
    lv_obj_invalidate(wheel_);
    lv_obj_invalidate(pointer_);
    UpdateFireworks(kSpinDurationMs);
}

}  // namespace kynangso
