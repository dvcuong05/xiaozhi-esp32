#include "display/tft/robot_lunar_calendar_view.h"

#include <algorithm>
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
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x101A2E), 0);
    lv_obj_set_style_bg_grad_color(root_, lv_color_hex(0x123B4A), 0);
    lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* glow = lv_obj_create(root_);
    lv_obj_remove_style_all(glow);
    lv_obj_set_size(glow, width * 3 / 4, height * 3 / 4);
    lv_obj_align(glow, LV_ALIGN_TOP_RIGHT, width / 4, -height / 4);
    lv_obj_set_style_bg_color(glow, lv_color_hex(0xE9B44C), 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_10, 0);
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(glow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(glow, LV_OBJ_FLAG_CLICKABLE);

    auto* card = lv_obj_create(root_);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, width - 20, height - 28);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x13243A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2DD4BF), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

    const int card_width = width - 20;
    CreateLabel(card, 8, 7, card_width - 16, 18, "ÂM LỊCH VIỆT NAM", 0xF4D58D,
                &robot_time_font_14, LV_TEXT_ALIGN_CENTER);
    day_ = CreateLabel(card, 8, 24, card_width - 16, 50, "1", 0xFFF7D6,
                       &robot_info_bold_36, LV_TEXT_ALIGN_CENTER);
    month_ = CreateLabel(card, 8, 74, card_width - 16, 25, "THÁNG 1 · 2026", 0x73E0CF,
                         &robot_info_bold_20, LV_TEXT_ALIGN_CENTER);
    leap_ = CreateLabel(card, 8, 99, card_width - 16, 14, "", 0xF4B860, &text_font_,
                        LV_TEXT_ALIGN_CENTER);

    event_icon_ = lv_obj_create(card);
    lv_obj_remove_style_all(event_icon_);
    lv_obj_set_size(event_icon_, 22, 22);
    lv_obj_set_pos(event_icon_, 13, 116);
    lv_obj_set_style_bg_color(event_icon_, lv_color_hex(0x3D3140), 0);
    lv_obj_set_style_bg_opa(event_icon_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(event_icon_, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(event_icon_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(event_icon_, LV_OBJ_FLAG_CLICKABLE);

    event_icon_mark_ = lv_obj_create(event_icon_);
    event_icon_detail_ = lv_obj_create(event_icon_);
    event_icon_tassel_ = lv_obj_create(event_icon_);
    for (auto* part : {event_icon_mark_, event_icon_detail_, event_icon_tassel_}) {
        lv_obj_remove_style_all(part);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
        lv_obj_remove_flag(part, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(part, LV_OBJ_FLAG_CLICKABLE);
    }
    event_title_ = CreateLabel(card, 42, 114, card_width - 55, 17, "Lịch âm Việt Nam",
                               0xFFF1C1, &text_font_, LV_TEXT_ALIGN_LEFT);
    event_count_ = CreateLabel(card, 42, 131, card_width - 55, 13, "Tính offline · UTC+7",
                               0x9CCBC8, &robot_time_font_14, LV_TEXT_ALIGN_LEFT);

    auto* divider = lv_obj_create(card);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, card_width - 48, 1);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 145);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xE9B44C), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_70, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);

    CreateLabel(card, 8, 151, card_width - 16, 15, "DƯƠNG LỊCH", 0x7EE4D1,
                &robot_time_font_14, LV_TEXT_ALIGN_CENTER);
    solar_ = CreateLabel(card, 8, 168, card_width - 16, 22, "01.01.2026", 0xFFF1C1,
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
    if (event_title_ != nullptr)
        lv_obj_set_style_text_font(event_title_, &text_font_, 0);
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
                                  const VietnameseLunarDate& lunar,
                                  const LunarCalendarEvents& events, size_t event_count,
                                  size_t total_event_count, bool events_loaded) {
    char solar[24];
    snprintf(solar, sizeof(solar), "%02d.%02d.%04d", solar_day, solar_month, solar_year);
    char month[32];
    snprintf(month, sizeof(month), "THÁNG %d · %d", lunar.month, lunar.year);
    lv_label_set_text_fmt(day_, "%d", lunar.day);
    lv_label_set_text(month_, month);
    lv_label_set_text(leap_, lunar.leap_month ? "THÁNG NHUẬN" : "");
    lv_label_set_text(solar_, solar);

    event_count = std::min(event_count, events.size());
    if (!events_loaded) {
        lv_label_set_text(event_title_, "Ngày âm lịch Việt Nam");
        lv_label_set_text(event_count_, "Tính offline · UTC+7");
        SetEventIcon(3);
    } else if (event_count == 0) {
        lv_label_set_text(event_title_, "Chưa có sự kiện hôm nay");
        lv_label_set_text(event_count_, "Lịch văn hóa Việt Nam");
        SetEventIcon(3);
    } else {
        const auto& event = events[0];
        lv_label_set_text(event_title_, event.title.data());
        char count_label[40];
        if (total_event_count > 1)
            snprintf(count_label, sizeof(count_label), "+%u sự kiện khác",
                     static_cast<unsigned>(total_event_count - 1));
        else
            snprintf(count_label, sizeof(count_label), "SỰ KIỆN ÂM LỊCH");
        lv_label_set_text(event_count_, count_label);
        SetEventIcon(event.icon);
    }
}

void RobotLunarCalendarView::SetEventIcon(uint8_t icon) {
    static constexpr uint32_t badge_colors[] = {0x432C31, 0x352B4B, 0x443821, 0x203F45};
    static constexpr uint32_t mark_colors[] = {0xF97316, 0xC084FC, 0xFBBF24, 0x2DD4BF};
    static constexpr uint32_t detail_colors[] = {0xFDE68A, 0xFBCFE8, 0xFFF1C1, 0xA7F3D0};
    icon = std::min<uint8_t>(icon, 3);
    lv_obj_set_style_bg_color(event_icon_, lv_color_hex(badge_colors[icon]), 0);
    lv_obj_set_style_bg_color(event_icon_mark_, lv_color_hex(mark_colors[icon]), 0);
    lv_obj_set_style_bg_color(event_icon_detail_, lv_color_hex(detail_colors[icon]), 0);
    lv_obj_set_style_bg_color(event_icon_tassel_, lv_color_hex(detail_colors[icon]), 0);

    if (icon == 0) {  // lantern
        lv_obj_set_size(event_icon_mark_, 8, 10);
        lv_obj_set_pos(event_icon_mark_, 7, 6);
        lv_obj_set_style_radius(event_icon_mark_, 3, 0);
        lv_obj_set_size(event_icon_detail_, 10, 2);
        lv_obj_set_pos(event_icon_detail_, 6, 4);
        lv_obj_set_style_radius(event_icon_detail_, 1, 0);
        lv_obj_set_size(event_icon_tassel_, 2, 3);
        lv_obj_set_pos(event_icon_tassel_, 10, 16);
        lv_obj_set_style_radius(event_icon_tassel_, 1, 0);
    } else if (icon == 1) {  // lotus / observance
        lv_obj_set_size(event_icon_mark_, 8, 8);
        lv_obj_set_pos(event_icon_mark_, 7, 8);
        lv_obj_set_style_radius(event_icon_mark_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(event_icon_detail_, 4, 8);
        lv_obj_set_pos(event_icon_detail_, 9, 4);
        lv_obj_set_style_radius(event_icon_detail_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(event_icon_tassel_, 12, 3);
        lv_obj_set_pos(event_icon_tassel_, 5, 16);
        lv_obj_set_style_radius(event_icon_tassel_, LV_RADIUS_CIRCLE, 0);
    } else if (icon == 2) {  // commemorative badge
        lv_obj_set_size(event_icon_mark_, 9, 9);
        lv_obj_set_pos(event_icon_mark_, 6, 7);
        lv_obj_set_style_radius(event_icon_mark_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(event_icon_detail_, 3, 13);
        lv_obj_set_pos(event_icon_detail_, 9, 4);
        lv_obj_set_style_radius(event_icon_detail_, 1, 0);
        lv_obj_set_size(event_icon_tassel_, 13, 3);
        lv_obj_set_pos(event_icon_tassel_, 4, 10);
        lv_obj_set_style_radius(event_icon_tassel_, 1, 0);
    } else {  // sun / general event
        lv_obj_set_size(event_icon_mark_, 8, 8);
        lv_obj_set_pos(event_icon_mark_, 7, 7);
        lv_obj_set_style_radius(event_icon_mark_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_size(event_icon_detail_, 3, 14);
        lv_obj_set_pos(event_icon_detail_, 9, 4);
        lv_obj_set_style_radius(event_icon_detail_, 1, 0);
        lv_obj_set_size(event_icon_tassel_, 14, 3);
        lv_obj_set_pos(event_icon_tassel_, 4, 9);
        lv_obj_set_style_radius(event_icon_tassel_, 1, 0);
    }
}
}  // namespace kynangso
