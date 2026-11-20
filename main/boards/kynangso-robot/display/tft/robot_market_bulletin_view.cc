#include "display/tft/robot_market_bulletin_view.h"

#include <algorithm>

LV_FONT_DECLARE(robot_time_font_14);
LV_FONT_DECLARE(robot_oled_font_10);

namespace kynangso {
namespace {

struct CategoryStyle {
    const char* title;
    uint32_t accent;
    uint32_t tint;
    uint32_t border;
    uint32_t surface_top;
    uint32_t surface_bottom;
    uint32_t row_primary;
    uint32_t row_secondary;
    bool show_vnd_header;
};

CategoryStyle StyleFor(const std::string& category) {
    if (category == "agriculture")
        return {"GIÁ NÔNG SẢN", 0x4ADE80, 0x123022, 0x244A36, 0x111D17,
                0x0B1410,       0x17271D, 0x121E17, true};
    if (category == "gold_silver")
        return {"GIÁ VÀNG", 0xFBBF24, 0x382A10, 0x57451B, 0x1E1A11,
                0x12110C,   0x292216, 0x201B12, true};
    if (category == "fuel")
        return {"GIÁ XĂNG DẦU", 0xFB923C, 0x3A2113, 0x58331D, 0x1E1712,
                0x12100E,       0x292016, 0x201912, true};
    return {"TỶ GIÁ NGOẠI TỆ", 0x38BDF8, 0x142D3D, 0x24465B, 0x111923,
            0x0B1119,          0x17232E, 0x121D27, false};
}

lv_obj_t* CreateLabel(lv_obj_t* parent, int x, int y, int width, int height, const lv_font_t* font,
                      uint32_t color, lv_label_long_mode_t long_mode) {
    auto* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_label_set_long_mode(label, long_mode);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(label, 1, 0);
    return label;
}

lv_obj_t* CreateShape(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color,
                      int radius = 0) {
    auto* shape = lv_obj_create(parent);
    lv_obj_remove_style_all(shape);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_style_bg_color(shape, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_CLICKABLE);
    return shape;
}

template <size_t N>
void AddIconLine(lv_obj_t* parent, int x, int y, const lv_point_precise_t (&points)[N],
                 uint32_t color, int line_width = 2) {
    auto* line = lv_line_create(parent);
    lv_line_set_points(line, points, N);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_line_width(line, line_width, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
}

lv_obj_t* CreateOutlineShape(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color,
                             int radius) {
    auto* shape = lv_obj_create(parent);
    lv_obj_remove_style_all(shape);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_style_bg_opa(shape, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(shape, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(shape, 2, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_CLICKABLE);
    return shape;
}

}  // namespace

RobotMarketBulletinView::RobotMarketBulletinView(int width, int height,
                                                 const lv_font_t* fallback_font)
    : width_(width), height_(height) {
    text_font_ = robot_time_font_14;
    text_font_.fallback = fallback_font;
    compact_font_ = robot_oled_font_10;
    compact_font_.fallback = fallback_font;
    Build();
}

RobotMarketBulletinView::~RobotMarketBulletinView() {
    StopScroll();
    if (root_ != nullptr)
        lv_obj_delete(root_);
}

void RobotMarketBulletinView::Build() {
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, width_, height_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x080B12), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    card_ = lv_obj_create(root_);
    lv_obj_remove_style_all(card_);
    lv_obj_set_pos(card_, 2, 2);
    lv_obj_set_size(card_, width_ - 4, height_ - 4);
    lv_obj_set_style_bg_color(card_, lv_color_hex(0x111923), 0);
    lv_obj_set_style_bg_grad_color(card_, lv_color_hex(0x0B1119), 0);
    lv_obj_set_style_bg_grad_dir(card_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_, lv_color_hex(0x293545), 0);
    lv_obj_set_style_border_width(card_, 1, 0);
    lv_obj_set_style_radius(card_, 10, 0);
    lv_obj_remove_flag(card_, LV_OBJ_FLAG_SCROLLABLE);

    icon_badge_ = CreateShape(card_, 6, 5, 26, 26, 0x142D3D, 8);
    title_ = CreateLabel(card_, 38, 5, width_ - 82, 20, &text_font_, 0xF4F8FC, LV_LABEL_LONG_DOT);
    sequence_ =
        CreateLabel(card_, width_ - 39, 8, 30, 14, &compact_font_, 0xC1D4E6, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(sequence_, LV_TEXT_ALIGN_RIGHT, 0);
    updated_at_ =
        CreateLabel(card_, 38, 23, width_ - 91, 13, &compact_font_, 0x91A9BE, LV_LABEL_LONG_DOT);
    currency_ =
        CreateLabel(card_, width_ - 48, 23, 38, 13, &compact_font_, 0x38BDF8, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(currency_, LV_TEXT_ALIGN_RIGHT, 0);
    divider_ = CreateShape(card_, 5, 38, width_ - 10, 1, 0x293545);

    rows_ = lv_obj_create(card_);
    lv_obj_remove_style_all(rows_);
    lv_obj_set_pos(rows_, 4, 41);
    lv_obj_set_size(rows_, width_ - 8, height_ - 46);
    lv_obj_set_style_bg_opa(rows_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(rows_, 0, 0);
    lv_obj_set_style_pad_row(rows_, 1, 0);
    lv_obj_set_flex_flow(rows_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rows_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(rows_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(rows_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(rows_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void RobotMarketBulletinView::BuildCategoryIcon(const std::string& category, uint32_t accent) {
    lv_obj_clean(icon_badge_);
    if (category == "agriculture") {
        static constexpr lv_point_precise_t stem[] = {{13, 21}, {13, 10}};
        static constexpr lv_point_precise_t left_leaf[] = {
            {13, 15}, {8, 13}, {6, 8}, {10, 7}, {13, 11}};
        static constexpr lv_point_precise_t right_leaf[] = {
            {13, 12}, {16, 7}, {20, 6}, {20, 10}, {13, 15}};
        AddIconLine(icon_badge_, 0, 0, stem, accent);
        AddIconLine(icon_badge_, 0, 0, left_leaf, accent);
        AddIconLine(icon_badge_, 0, 0, right_leaf, accent);
    } else if (category == "gold_silver") {
        auto* coin = CreateOutlineShape(icon_badge_, 5, 5, 16, 16, accent, 8);
        auto* inner = CreateOutlineShape(coin, 3, 3, 8, 8, accent, 4);
        lv_obj_set_style_border_width(inner, 1, 0);
    } else if (category == "fuel") {
        auto* pump = CreateOutlineShape(icon_badge_, 6, 4, 12, 17, accent, 2);
        CreateShape(pump, 2, 3, 5, 4, accent, 1);
        CreateShape(pump, 4, 15, 6, 2, accent, 1);
        static constexpr lv_point_precise_t hose[] = {{18, 7}, {21, 9}, {21, 16}, {19, 18}};
        AddIconLine(icon_badge_, 0, 0, hose, accent);
    } else {
        static constexpr lv_point_precise_t forward[] = {{5, 8}, {20, 8}};
        static constexpr lv_point_precise_t forward_head[] = {{16, 4}, {20, 8}, {16, 12}};
        static constexpr lv_point_precise_t reverse[] = {{20, 18}, {5, 18}};
        static constexpr lv_point_precise_t reverse_head[] = {{9, 14}, {5, 18}, {9, 22}};
        AddIconLine(icon_badge_, 0, 0, forward, accent);
        AddIconLine(icon_badge_, 0, 0, forward_head, accent);
        AddIconLine(icon_badge_, 0, 0, reverse, accent);
        AddIconLine(icon_badge_, 0, 0, reverse_head, accent);
    }
}

void RobotMarketBulletinView::SetTile(const MarketBulletinTile& tile, size_t index, size_t total) {
    StopScroll();
    const CategoryStyle style = StyleFor(tile.category);
    lv_obj_set_style_bg_color(card_, lv_color_hex(style.surface_top), 0);
    lv_obj_set_style_bg_grad_color(card_, lv_color_hex(style.surface_bottom), 0);
    lv_obj_set_style_border_color(card_, lv_color_hex(style.border), 0);
    lv_obj_set_style_bg_color(icon_badge_, lv_color_hex(style.tint), 0);
    lv_obj_set_style_bg_color(divider_, lv_color_hex(style.border), 0);
    BuildCategoryIcon(tile.category, style.accent);
    lv_label_set_text(title_, style.title);
    lv_label_set_text_fmt(sequence_, "%u/%u", static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
    std::string update_text;
    if (!tile.updated_at.empty())
        update_text = tile.stale ? "C\u0168 \u00b7 " + tile.updated_at
                                 : "C\u1EACP NH\u1EACT " + tile.updated_at;
    lv_label_set_text(updated_at_, update_text.c_str());
    lv_obj_set_style_text_color(updated_at_, lv_color_hex(tile.stale ? 0xFCD34D : 0x91A9BE), 0);
    lv_label_set_text(currency_, style.show_vnd_header ? "VND" : "");
    lv_obj_set_style_text_color(currency_, lv_color_hex(style.accent), 0);

    lv_obj_clean(rows_);
    lv_obj_scroll_to_y(rows_, 0, LV_ANIM_OFF);
    if (!tile.available || tile.row_count == 0) {
        auto* empty = lv_label_create(rows_);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_font(empty, &text_font_, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD7E3EE), 0);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_DOT);
        lv_label_set_text(empty, "Ch\u01b0a c\u00f3 d\u1eef li\u1ec7u.");
        return;
    }

    const size_t row_count = std::min(tile.row_count, tile.rows.size());
    for (size_t i = 0; i < row_count; ++i) {
        auto* row = lv_obj_create(rows_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 23);
        lv_obj_set_style_bg_color(
            row, lv_color_hex(i % 2 == 0 ? style.row_primary : style.row_secondary), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_hor(row, 3, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* label = lv_label_create(row);
        const int row_content_width = width_ - 16;
        lv_obj_set_size(label, row_content_width * 44 / 100, 14);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 3, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, tile.rows[i].label.c_str());
        lv_obj_set_style_text_font(label, &compact_font_, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xD5E2EE), 0);

        auto* value = lv_label_create(row);
        lv_obj_set_size(value, row_content_width * 52 / 100, 14);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, -3, 0);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        std::string display_value = tile.rows[i].value;
        if (style.show_vnd_header) {
            const size_t currency = display_value.find(" VND");
            if (currency != std::string::npos)
                display_value.erase(currency, 4);
        }
        lv_label_set_text(value, display_value.c_str());
        lv_obj_set_style_text_font(value, &compact_font_, 0);
        lv_obj_set_style_text_color(value, lv_color_hex(style.accent), 0);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    }
    StartScroll();
}

void RobotMarketBulletinView::SetVisible(bool visible) {
    if (root_ == nullptr)
        return;
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        if (scroll_timer_ != nullptr)
            lv_timer_resume(scroll_timer_);
    } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        if (scroll_timer_ != nullptr)
            lv_timer_pause(scroll_timer_);
    }
}

void RobotMarketBulletinView::StopScroll() {
    if (scroll_timer_ != nullptr) {
        lv_timer_delete(scroll_timer_);
        scroll_timer_ = nullptr;
    }
    scroll_index_ = 0;
    first_scroll_index_ = 0;
}

void RobotMarketBulletinView::StartScroll() {
    lv_obj_update_layout(rows_);
    if (lv_obj_get_scroll_bottom(rows_) <= 0)
        return;
    const int32_t viewport_height = lv_obj_get_height(rows_);
    const size_t row_count = lv_obj_get_child_count(rows_);
    while (first_scroll_index_ < row_count) {
        lv_obj_t* row = lv_obj_get_child(rows_, static_cast<int32_t>(first_scroll_index_));
        if (row == nullptr || lv_obj_get_y(row) + lv_obj_get_height(row) > viewport_height)
            break;
        ++first_scroll_index_;
    }
    if (first_scroll_index_ >= row_count)
        return;
    scroll_index_ = first_scroll_index_;
    scroll_timer_ = lv_timer_create(OnScrollTimer, 950, this);
    if (scroll_timer_ != nullptr && lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN))
        lv_timer_pause(scroll_timer_);
}

void RobotMarketBulletinView::OnScrollTimer(lv_timer_t* timer) {
    auto* self = static_cast<RobotMarketBulletinView*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->root_ == nullptr || self->rows_ == nullptr ||
        lv_obj_has_flag(self->root_, LV_OBJ_FLAG_HIDDEN))
        return;
    const size_t row_count = lv_obj_get_child_count(self->rows_);
    if (row_count == 0)
        return;
    if (self->scroll_index_ >= row_count) {
        lv_obj_scroll_to_y(self->rows_, 0, LV_ANIM_ON);
        self->scroll_index_ = self->first_scroll_index_;
        return;
    }
    lv_obj_t* row = lv_obj_get_child(self->rows_, static_cast<int32_t>(self->scroll_index_++));
    if (row != nullptr)
        lv_obj_scroll_to_view(row, LV_ANIM_ON);
}

}  // namespace kynangso
