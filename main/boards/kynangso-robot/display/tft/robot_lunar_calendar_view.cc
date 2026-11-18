#include "display/tft/robot_lunar_calendar_view.h"

#include <cstdio>

LV_FONT_DECLARE(robot_time_font_14);
LV_FONT_DECLARE(robot_info_bold_20);
LV_FONT_DECLARE(robot_info_bold_36);

namespace kynangso {
namespace {
lv_obj_t* CreateLabel(lv_obj_t* parent, int x, int y, int width, int height, const char* text,
                      uint32_t color, const lv_font_t* font, lv_text_align_t align) {
    auto* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}
}  // namespace

RobotLunarCalendarView::RobotLunarCalendarView(int width, int height, const lv_font_t* font) {
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, width, height);
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x10162B), 0);
    lv_obj_set_style_bg_grad_color(root_, lv_color_hex(0x31204A), 0);
    lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* glow = lv_obj_create(root_);
    lv_obj_remove_style_all(glow);
    lv_obj_set_size(glow, width * 3 / 4, height * 3 / 4);
    lv_obj_align(glow, LV_ALIGN_TOP_RIGHT, width / 4, -height / 4);
    lv_obj_set_style_bg_color(glow, lv_color_hex(0x8B5CF6), 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_10, 0);
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(glow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(glow, LV_OBJ_FLAG_CLICKABLE);

    auto* card = lv_obj_create(root_);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, width - 20, height - 28);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x171D35), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x7760A8), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

    const int card_width = width - 20;
    CreateLabel(card, 8, 13, card_width - 16, 18, "ÂM LỊCH VIỆT NAM", 0xC4B5FD,
                &robot_time_font_14, LV_TEXT_ALIGN_CENTER);
    day_ = CreateLabel(card, 8, 37, card_width - 16, 66, "1", 0xFFF7D6,
                       &robot_info_bold_36, LV_TEXT_ALIGN_CENTER);
    month_ = CreateLabel(card, 8, 101, card_width - 16, 27, "THÁNG 1 · 2026", 0xF5D0FE,
                         &robot_info_bold_20, LV_TEXT_ALIGN_CENTER);
    leap_ = CreateLabel(card, 8, 130, card_width - 16, 17, "", 0xFBBF24, &text_font_,
                        LV_TEXT_ALIGN_CENTER);

    auto* divider = lv_obj_create(card);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, card_width - 48, 1);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 153);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x8067B2), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_50, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);

    CreateLabel(card, 8, 161, card_width - 16, 15, "DƯƠNG LỊCH", 0x9AA7C8,
                &robot_time_font_14, LV_TEXT_ALIGN_CENTER);
    solar_ = CreateLabel(card, 8, 178, card_width - 16, 22, "01.01.2026", 0xE7E9F5,
                         &robot_info_bold_20, LV_TEXT_ALIGN_CENTER);
    SetFont(font);
    SetVisible(false);
}

RobotLunarCalendarView::~RobotLunarCalendarView() {
    if (root_ != nullptr)
        lv_obj_delete(root_);
}

void RobotLunarCalendarView::SetFont(const lv_font_t* font) {
    text_font_ = robot_time_font_14;
    text_font_.fallback = font;
    if (leap_ != nullptr)
        lv_obj_set_style_text_font(leap_, &text_font_, 0);
}

void RobotLunarCalendarView::SetVisible(bool visible) {
    if (root_ == nullptr)
        return;
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RobotLunarCalendarView::Show(int solar_day, int solar_month, int solar_year,
                                  const VietnameseLunarDate& lunar) {
    char solar[24];
    snprintf(solar, sizeof(solar), "%02d.%02d.%04d", solar_day, solar_month, solar_year);
    char month[32];
    snprintf(month, sizeof(month), "THÁNG %d · %d", lunar.month, lunar.year);
    lv_label_set_text_fmt(day_, "%d", lunar.day);
    lv_label_set_text(month_, month);
    lv_label_set_text(leap_, lunar.leap_month ? "THÁNG NHUẬN" : "");
    lv_label_set_text(solar_, solar);
}
}  // namespace kynangso
