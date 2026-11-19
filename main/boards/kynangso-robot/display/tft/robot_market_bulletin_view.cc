#include "display/tft/robot_market_bulletin_view.h"

#include <algorithm>

LV_FONT_DECLARE(robot_time_font_14);
LV_FONT_DECLARE(robot_info_bold_20);

namespace kynangso {
namespace {

struct CategoryStyle {
    const char* title;
    uint32_t accent;
};

CategoryStyle StyleFor(const std::string& category) {
    if (category == "agriculture")
        return {"GIÁ NÔNG SẢN", 0x4ADE80};
    if (category == "gold_silver")
        return {"GIÁ VÀNG", 0xFBBF24};
    if (category == "fuel")
        return {"GIÁ XĂNG DẦU", 0xFB923C};
    return {"TỶ GIÁ NGOẠI TỆ", 0x38BDF8};
}

lv_obj_t* CreateLabel(lv_obj_t* parent, int x, int y, int width, int height,
                      const lv_font_t* font, uint32_t color, lv_label_long_mode_t long_mode) {
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

}  // namespace

RobotMarketBulletinView::RobotMarketBulletinView(int width, int height,
                                                 const lv_font_t* fallback_font)
    : width_(width), height_(height) {
    text_font_ = robot_time_font_14;
    text_font_.fallback = fallback_font;
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
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x071522), 0);
    lv_obj_set_style_bg_grad_color(root_, lv_color_hex(0x12253A), 0);
    lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* card = lv_obj_create(root_);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 8, 8);
    lv_obj_set_size(card, width_ - 16, height_ - 16);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x102237), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x0B1929), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x29445B), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    accent_ = CreateShape(card, 12, 12, 4, 57, 0x38BDF8, 3);
    auto* eyebrow = CreateLabel(card, 24, 10, width_ - 60, 17, &text_font_, 0x9AB4CC,
                                LV_LABEL_LONG_DOT);
    lv_label_set_text(eyebrow, "BẢN TIN THỊ TRƯỜNG");
    title_ = CreateLabel(card, 23, 28, width_ - 77, 27, &robot_info_bold_20, 0xF4F8FC,
                         LV_LABEL_LONG_DOT);
    sequence_ = CreateLabel(card, width_ - 55, 31, 33, 18, &text_font_, 0xC1D4E6,
                            LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(sequence_, LV_TEXT_ALIGN_RIGHT, 0);
    source_ = CreateLabel(card, 23, 57, width_ - 73, 18, &text_font_, 0xA9BED2,
                          LV_LABEL_LONG_DOT);
    stale_badge_ = CreateLabel(card, width_ - 53, 57, 36, 18, &text_font_, 0xFBBF24,
                               LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(stale_badge_, LV_TEXT_ALIGN_RIGHT, 0);
    CreateShape(card, 20, 81, width_ - 56, 1, 0x29445B);

    rows_ = lv_obj_create(card);
    lv_obj_remove_style_all(rows_);
    lv_obj_set_pos(rows_, 20, 90);
    lv_obj_set_size(rows_, width_ - 56, height_ - 131);
    lv_obj_set_style_bg_opa(rows_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(rows_, 0, 0);
    lv_obj_set_style_pad_row(rows_, 5, 0);
    lv_obj_set_flex_flow(rows_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rows_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_flag(rows_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(rows_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(rows_, LV_SCROLLBAR_MODE_OFF);

    footer_ = CreateLabel(card, 20, height_ - 33, width_ - 56, 16, &text_font_, 0x7894AD,
                          LV_LABEL_LONG_DOT);
    lv_label_set_text(footer_, "GIÁ THAM KHẢO · KY NĂNG SỐ");
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void RobotMarketBulletinView::SetTile(const MarketBulletinTile& tile, size_t index,
                                      size_t total) {
    StopScroll();
    const CategoryStyle style = StyleFor(tile.category);
    lv_obj_set_style_bg_color(accent_, lv_color_hex(style.accent), 0);
    lv_label_set_text(title_, style.title);
    lv_label_set_text_fmt(sequence_, "%u/%u", static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
    std::string source_text = tile.source.empty() ? "Chưa có dữ liệu đã crawl" : tile.source;
    if (!tile.updated_at.empty())
        source_text += " · " + tile.updated_at;
    lv_label_set_text(source_, source_text.c_str());
    lv_label_set_text(stale_badge_, tile.stale ? "CŨ" : "");
    lv_obj_set_style_text_color(source_, lv_color_hex(tile.stale ? 0xFCD34D : 0xA9BED2), 0);

    lv_obj_clean(rows_);
    lv_obj_scroll_to_y(rows_, 0, LV_ANIM_OFF);
    if (!tile.available || tile.row_count == 0) {
        auto* empty = lv_label_create(rows_);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_font(empty, &text_font_, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD7E3EE), 0);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_label_set_text(empty, "Chưa có báo giá đã crawl cho nhóm này.");
        return;
    }

    const size_t row_count = std::min(tile.row_count, tile.rows.size());
    for (size_t i = 0; i < row_count; ++i) {
        auto* row = lv_obj_create(rows_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 29);
        lv_obj_set_style_bg_color(row, lv_color_hex(i % 2 == 0 ? 0x172C41 : 0x13263A), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 7, 0);
        lv_obj_set_style_pad_hor(row, 7, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* label = lv_label_create(row);
        const int row_content_width = width_ - 70;
        lv_obj_set_width(label, row_content_width * 50 / 100);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, tile.rows[i].label.c_str());
        lv_obj_set_style_text_font(label, &text_font_, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xD5E2EE), 0);

        auto* value = lv_label_create(row);
        lv_obj_set_width(value, row_content_width * 46 / 100);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_label_set_text(value, tile.rows[i].value.c_str());
        lv_obj_set_style_text_font(value, &text_font_, 0);
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
