#include "display/tft/robot_time_view.h"
#include <algorithm>
LV_FONT_DECLARE(robot_time_font_14);
LV_FONT_DECLARE(robot_info_bold_36);
LV_FONT_DECLARE(robot_info_bold_20);
namespace kynangso {
RobotTimeView::RobotTimeView(int w, int h, const lv_font_t* font) {
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root_, w, h);
    lv_obj_center(root_);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_grad_color(root_, lv_color_hex(0x241A3D), 0);
    lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    heading_ = lv_label_create(root_);
    lv_obj_set_pos(heading_, 12, 8);
    lv_obj_set_size(heading_, w - 24, 24);
    ring_ = lv_arc_create(root_);
    lv_obj_set_size(ring_, 150, 150);
    lv_obj_align(ring_, LV_ALIGN_TOP_MID, 0, 34);
    lv_arc_set_rotation(ring_, 270);
    lv_arc_set_bg_angles(ring_, 0, 360);
    lv_arc_set_range(ring_, 0, 1000);
    lv_obj_remove_style(ring_, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(ring_, lv_color_hex(0x2B3548), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring_, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring_, 7, LV_PART_INDICATOR);
    digits_ = lv_label_create(root_);
    lv_obj_remove_style_all(digits_);
    lv_obj_set_size(digits_, w - 36, 46);
    lv_obj_align(digits_, LV_ALIGN_TOP_MID, 0, 87);
    lv_obj_set_style_text_align(digits_, LV_TEXT_ALIGN_CENTER, 0);
    title_ = lv_label_create(root_);
    lv_obj_set_pos(title_, 12, h - 54);
    lv_obj_set_size(title_, w - 24, 26);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    footer_ = lv_label_create(root_);
    lv_obj_set_pos(footer_, 8, h - 26);
    lv_obj_set_size(footer_, w - 16, 24);
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_DOT);
    for (auto* label : {heading_, title_, footer_}) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xF1F5F9), 0);
    }
    lv_obj_set_style_text_color(footer_, lv_color_hex(0x94A3B8), 0);
    SetFont(font);
    SetVisible(false);
}
RobotTimeView::~RobotTimeView() { lv_obj_delete(root_); }
void RobotTimeView::SetFont(const lv_font_t* font) {
    text_font_ = robot_time_font_14;
    text_font_.fallback = font;
    for (auto* label : {heading_, title_, footer_})
        lv_obj_set_style_text_font(label, &text_font_, 0);
}
void RobotTimeView::SetVisible(bool visible) {
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    } else
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}
void RobotTimeView::Update(const TimeView& v) {
    const char* heading = "ĐẾM NGƯỢC";
    accent_ = 0x38BDF8;
    uint32_t gradient = 0x132B46;
    if (v.mode == "focus") {
        heading = "TẬP TRUNG";
        accent_ = 0xC4A2FF;
        gradient = 0x32204D;
    } else if (v.mode == "break") {
        heading = "NGHỈ NGƠI";
        accent_ = 0x4ADEAB;
        gradient = 0x143D35;
    } else if (v.mode == "alarm") {
        heading = "BÁO THỨC";
        accent_ = 0xFFBC66;
        gradient = 0x46281D;
    } else if (v.mode == "reminder") {
        heading = "NHẮC VIỆC";
        accent_ = 0x7DD3FC;
        gradient = 0x193551;
    }
    lv_label_set_text(heading_, v.alert ? "ĐẾN GIỜ RỒI!" : heading);
    lv_obj_set_style_text_color(heading_, lv_color_hex(accent_), 0);
    lv_obj_set_style_bg_grad_color(root_, lv_color_hex(gradient), 0);
    lv_obj_set_style_arc_color(ring_, lv_color_hex(accent_), LV_PART_INDICATOR);
    lv_obj_set_style_opa(ring_, v.paused ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_arc_set_value(ring_, std::clamp(v.progress, 0, 1000));
    lv_label_set_text(title_, v.title.c_str());
    lv_label_set_text(footer_, v.subtitle.c_str());
    number_ = v.digits;
    lv_obj_set_style_text_font(digits_,
                               number_.size() > 5 ? &robot_info_bold_20 : &robot_info_bold_36, 0);
    lv_obj_set_style_text_color(digits_, lv_color_hex(accent_), 0);
    lv_label_set_text(digits_, number_.c_str());
}
}  // namespace kynangso
