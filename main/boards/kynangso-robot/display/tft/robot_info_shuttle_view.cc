#include "display/tft/robot_info_shuttle_view.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

LV_FONT_DECLARE(robot_time_font_14);
LV_FONT_DECLARE(robot_info_font_16);
LV_FONT_DECLARE(robot_info_bold_20);
LV_FONT_DECLARE(robot_info_bold_36);

namespace kynangso {
namespace {

struct CardPalette {
    uint32_t schedule_start, schedule_end, schedule_glow, schedule_glow_secondary;
    uint32_t schedule_header, schedule_accent, schedule_footer_bg, schedule_footer_text;
    uint32_t schedule_icon_bg, schedule_icon_top, schedule_icon_dots;
    uint32_t weather_start, weather_end, weather_glow, weather_glow_secondary;
    uint32_t weather_header, weather_accent, weather_icon_bg, weather_sun, weather_sunlight;
};

constexpr CardPalette kCardPalettes[] = {
    {0x0A1630, 0x182D50, 0x2563EB, 0x7C3AED, 0x93C5FD, 0x67E8F9, 0x14385A,
     0xBAE6FD, 0x1E40AF, 0x38BDF8, 0x2563EB, 0x06283A, 0x14556A, 0x0EA5E9,
     0x22D3EE, 0xA5F3FC, 0x67E8F9, 0x0B4055, 0xFBBF24, 0xFDE68A},
    {0x071D1A, 0x123D34, 0x10B981, 0x0F766E, 0xA7F3D0, 0x6EE7B7, 0x12483B,
     0xBBF7D0, 0x047857, 0x34D399, 0x10B981, 0x08291F, 0x174936, 0x10B981,
     0x34D399, 0xA7F3D0, 0x6EE7B7, 0x104A38, 0xA3E635, 0xD9F99D},
    {0x17132B, 0x34234E, 0x8B5CF6, 0x6D28D9, 0xDDD6FE, 0xC4B5FD, 0x38245A,
     0xE9D5FF, 0x6D28D9, 0xC4B5FD, 0xA78BFA, 0x201634, 0x47265A, 0xA855F7,
     0xC084FC, 0xE9D5FF, 0xD8B4FE, 0x43205C, 0xF0ABFC, 0xF5D0FE},
    {0x21151A, 0x4B2528, 0xF97316, 0xBE123C, 0xFED7AA, 0xFDBA74, 0x552B29,
     0xFFEDD5, 0xC2410C, 0xFDBA74, 0xFB7185, 0x28191D, 0x58302E, 0xF97316,
     0xFB7185, 0xFED7AA, 0xFDBA74, 0x542A2A, 0xFDE047, 0xFEF08A},
};

const CardPalette& Palette(int color_theme) {
    const int index = std::clamp(color_theme, 0, 3);
    return kCardPalettes[index];
}

lv_obj_t* Shape(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color,
                int radius = 6, lv_opa_t opacity = LV_OPA_COVER) {
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

lv_obj_t* Label(lv_obj_t* parent, int x, int y, int width, int height, const char* text,
                uint32_t color, const lv_font_t* font, lv_text_align_t align = LV_TEXT_ALIGN_LEFT,
                lv_label_long_mode_t long_mode = LV_LABEL_LONG_DOT) {
    auto* object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_label_set_text(object, text);
    lv_label_set_long_mode(object, long_mode);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(object, align, 0);
    lv_obj_set_style_text_line_space(object, 2, 0);
    return object;
}

lv_obj_t* IconGroup(lv_obj_t* parent, int x, int y) {
    auto* group = lv_obj_create(parent);
    lv_obj_remove_style_all(group);
    lv_obj_set_pos(group, x, y);
    lv_obj_set_size(group, 58, 58);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_CLICKABLE);
    return group;
}

void AddCloud(lv_obj_t* group) {
    Shape(group, 11, 20, 25, 19, 0xDCEBFA, LV_RADIUS_CIRCLE);
    Shape(group, 20, 14, 23, 25, 0xF8FAFC, LV_RADIUS_CIRCLE);
    Shape(group, 31, 22, 17, 16, 0xEAF4FF, LV_RADIUS_CIRCLE);
    Shape(group, 9, 29, 42, 15, 0xF8FAFC, 8);
}

void StylePage(lv_obj_t* page, int width, int height, uint32_t start, uint32_t end) {
    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, width, height);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_grad_color(page, lv_color_hex(end), 0);
    lv_obj_set_style_bg_grad_dir(page, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_CLICKABLE);
}

void Trim(std::string& value) {
    while (!value.empty() &&
           (std::isspace(static_cast<unsigned char>(value.front())) || value.front() == ',' ||
            value.front() == ':' || value.front() == '|' || value.front() == '-')) {
        value.erase(value.begin());
    }
    while (!value.empty() &&
           (std::isspace(static_cast<unsigned char>(value.back())) || value.back() == ',' ||
            value.back() == ':' || value.back() == '|' || value.back() == '-')) {
        value.pop_back();
    }
}

}  // namespace

RobotInfoShuttleView::RobotInfoShuttleView(int width, int height, const lv_font_t* font,
                                           int calendar_theme, int weather_theme)
    : width_(width),
      height_(height),
      calendar_theme_(std::clamp(calendar_theme, 0, 3)),
      weather_theme_(std::clamp(weather_theme, 0, 3)) {
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, width_, height_);
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    SetFont(font);

    face_page_ = lv_obj_create(root_);
    lv_obj_remove_style_all(face_page_);
    lv_obj_set_size(face_page_, width_, height_);
    lv_obj_set_style_bg_opa(face_page_, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(face_page_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(face_page_, LV_OBJ_FLAG_CLICKABLE);

    schedule_page_ = lv_obj_create(root_);
    StylePage(schedule_page_, width_, height_, Palette(calendar_theme_).schedule_start,
              Palette(calendar_theme_).schedule_end);
    BuildSchedulePage();

    weather_page_ = lv_obj_create(root_);
    StylePage(weather_page_, width_, height_, Palette(weather_theme_).weather_start,
              Palette(weather_theme_).weather_end);
    BuildWeatherPage();

    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    SelectPage(face_page_);
}

RobotInfoShuttleView::~RobotInfoShuttleView() {
    StopScheduleScroll();
    StopWeatherAnimation();
    if (root_ != nullptr)
        lv_obj_delete(root_);
}

void RobotInfoShuttleView::BuildSchedulePage() {
    const auto& palette = Palette(calendar_theme_);
    const int icon_x = 16;
    const int icon_y = 13;
    Shape(schedule_page_, icon_x, icon_y, 22, 22, 0xEAF4FF, 4);
    Shape(schedule_page_, icon_x, icon_y, 22, 7, palette.schedule_icon_top, 3);
    Shape(schedule_page_, icon_x + 4, icon_y - 3, 4, 8, 0xE0F2FE, 2);
    Shape(schedule_page_, icon_x + 14, icon_y - 3, 4, 8, 0xE0F2FE, 2);
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            Shape(schedule_page_, icon_x + 4 + column * 6, icon_y + 11 + row * 6, 3, 3,
                  palette.schedule_icon_dots, LV_RADIUS_CIRCLE);
        }
    }

    schedule_title_ = Label(schedule_page_, 46, 8, 91, 28, "Calendar", palette.schedule_header,
                            &robot_info_bold_20);
    schedule_heading_ =
        Label(schedule_page_, 140, 13, width_ - 151, 22, "- What's next", 0xF1F5F9, &text_font_);

    schedule_list_ = lv_obj_create(schedule_page_);
    lv_obj_remove_style_all(schedule_list_);
    lv_obj_set_pos(schedule_list_, 16, 49);
    lv_obj_set_size(schedule_list_, width_ - 32, height_ - 59);
    lv_obj_set_style_bg_opa(schedule_list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(schedule_list_, 0, 0);
    lv_obj_set_style_pad_all(schedule_list_, 0, 0);
    lv_obj_set_style_pad_row(schedule_list_, 8, 0);
    lv_obj_set_flex_flow(schedule_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(schedule_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_flag(schedule_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(schedule_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(schedule_list_, LV_SCROLLBAR_MODE_OFF);
}

void RobotInfoShuttleView::BuildWeatherPage() {
    const auto& palette = Palette(weather_theme_);
    Label(weather_page_, 17, 12, width_ - 34, 22, "Weather", palette.weather_header, &title_font_);

    weather_temperature_ =
        Label(weather_page_, 17, 52, width_ - 102, 46, "--°", 0xF8FAFC, large_font_);
    weather_location_ =
        Label(weather_page_, 17, 106, width_ - 34, 52, "Đang chờ dữ liệu..", palette.weather_accent,
              &title_font_, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_WRAP);
    weather_condition_ =
        Label(weather_page_, 17, 160, width_ - 34, height_ - 205, "Thời tiết sẽ hiện tại đây",
              0xE2E8F0, &text_font_, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_WRAP);

    Shape(weather_page_, width_ - 81, 50, 63, 63, palette.weather_icon_bg, LV_RADIUS_CIRCLE,
          LV_OPA_80);
    const int icon_x = width_ - 78;
    const int icon_y = 52;
    weather_icon_sun_ = IconGroup(weather_page_, icon_x, icon_y);
    Shape(weather_icon_sun_, 17, 17, 24, 24, palette.weather_sun, LV_RADIUS_CIRCLE);
    Shape(weather_icon_sun_, 27, 6, 4, 8, palette.weather_sunlight, 2);
    Shape(weather_icon_sun_, 27, 44, 4, 8, palette.weather_sunlight, 2);
    Shape(weather_icon_sun_, 6, 27, 8, 4, palette.weather_sunlight, 2);
    Shape(weather_icon_sun_, 44, 27, 8, 4, palette.weather_sunlight, 2);
    Shape(weather_icon_sun_, 12, 12, 5, 5, palette.weather_sunlight, LV_RADIUS_CIRCLE);
    Shape(weather_icon_sun_, 41, 41, 5, 5, palette.weather_sunlight, LV_RADIUS_CIRCLE);

    weather_icon_cloud_ = IconGroup(weather_page_, icon_x, icon_y);
    AddCloud(weather_icon_cloud_);

    weather_icon_storm_ = IconGroup(weather_page_, icon_x, icon_y);
    AddCloud(weather_icon_storm_);
    for (int i = 0; i < 3; ++i) {
        weather_icon_storm_rain_[i] =
            Shape(weather_icon_storm_, 15 + i * 12, 43, 4, 9, 0x54C8FF, 2);
    }
    weather_icon_flash_ = Shape(weather_icon_storm_, 28, 32, 8, 15, 0xFDE047, 2);

    weather_icon_rain_group_ = IconGroup(weather_page_, icon_x, icon_y);
    AddCloud(weather_icon_rain_group_);
    for (int i = 0; i < 3; ++i) {
        weather_icon_rain_[i] = Shape(weather_icon_rain_group_, 15 + i * 12, 43, 4, 9, 0x54C8FF, 2);
    }

    weather_icon_wind_group_ = IconGroup(weather_page_, icon_x, icon_y);
    Shape(weather_icon_wind_group_, 8, 15, 37, 5, 0xDCEBFA, 3);
    Shape(weather_icon_wind_group_, 15, 26, 36, 5, 0xF8FAFC, 3);
    Shape(weather_icon_wind_group_, 7, 37, 29, 5, 0xA5D8FF, 3);

    weather_icon_snow_ = IconGroup(weather_page_, icon_x, icon_y);
    AddCloud(weather_icon_snow_);
    for (int i = 0; i < 3; ++i) {
        Shape(weather_icon_snow_, 15 + i * 13, 44, 5, 5, 0xBFEAFF, LV_RADIUS_CIRCLE);
    }

    weather_icon_fog_ = IconGroup(weather_page_, icon_x, icon_y);
    AddCloud(weather_icon_fog_);
    Shape(weather_icon_fog_, 8, 43, 42, 4, 0xDCEBFA, 2);
    Shape(weather_icon_fog_, 14, 50, 31, 4, 0xA5D8FF, 2);
    for (auto* icon :
         {weather_icon_sun_, weather_icon_cloud_, weather_icon_storm_, weather_icon_rain_group_,
          weather_icon_wind_group_, weather_icon_snow_, weather_icon_fog_}) {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }

    auto* footer = Shape(weather_page_, 16, height_ - 40, 166, 24, palette.schedule_footer_bg, 12);
    weather_footer_ =
        Label(footer, 8, 4, 150, 16, "BẢN TIN TRỰC TIẾP", palette.weather_header, &text_font_);
    Shape(weather_page_, width_ - 40, height_ - 29, 5, 5, 0x22D3EE, LV_RADIUS_CIRCLE);
    Shape(weather_page_, width_ - 30, height_ - 29, 5, 5, 0x40526A, LV_RADIUS_CIRCLE);
}

void RobotInfoShuttleView::SetFont(const lv_font_t* font) {
    text_font_ = robot_time_font_14;
    text_font_.fallback = font;
    title_font_ = robot_info_font_16;
    title_font_.fallback = &text_font_;
    large_font_ = font != nullptr ? font : &text_font_;

    for (auto* label :
         {schedule_heading_, weather_location_, weather_condition_, weather_footer_}) {
        if (label != nullptr)
            lv_obj_set_style_text_font(label, &text_font_, 0);
    }
    if (schedule_title_ != nullptr)
        lv_obj_set_style_text_font(schedule_title_, &robot_info_bold_20, 0);
    if (schedule_heading_ != nullptr)
        lv_obj_set_style_text_font(schedule_heading_, &title_font_, 0);
    if (weather_location_ != nullptr)
        lv_obj_set_style_text_font(weather_location_, &title_font_, 0);
    if (weather_temperature_ != nullptr)
        lv_obj_set_style_text_font(weather_temperature_, large_font_, 0);
}

void RobotInfoShuttleView::SetColorThemes(int calendar_theme, int weather_theme) {
    calendar_theme = std::clamp(calendar_theme, 0, 3);
    weather_theme = std::clamp(weather_theme, 0, 3);
    if (calendar_theme == calendar_theme_ && weather_theme == weather_theme_)
        return;

    calendar_theme_ = calendar_theme;
    weather_theme_ = weather_theme;
    StopWeatherAnimation();
    StopScheduleScroll();
    if (schedule_page_ != nullptr)
        lv_obj_delete(schedule_page_);
    if (weather_page_ != nullptr)
        lv_obj_delete(weather_page_);
    schedule_page_ = lv_obj_create(root_);
    StylePage(schedule_page_, width_, height_, Palette(calendar_theme_).schedule_start,
              Palette(calendar_theme_).schedule_end);
    BuildSchedulePage();
    weather_page_ = lv_obj_create(root_);
    StylePage(weather_page_, width_, height_, Palette(weather_theme_).weather_start,
              Palette(weather_theme_).weather_end);
    BuildWeatherPage();
    SetFont(large_font_);
    SelectPage(face_page_);
}

void RobotInfoShuttleView::SelectPage(lv_obj_t* page) {
    if (page != weather_page_)
        StopWeatherAnimation();
    if (page != schedule_page_)
        StopScheduleScroll();
    for (auto* candidate : {face_page_, schedule_page_, weather_page_}) {
        if (candidate == nullptr)
            continue;
        if (candidate == page)
            lv_obj_remove_flag(candidate, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(candidate, LV_OBJ_FLAG_HIDDEN);
    }
    if (page == weather_page_ && root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN))
        StartWeatherAnimation();
    if (page == schedule_page_ && root_ != nullptr && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN))
        StartScheduleScroll();
}

void RobotInfoShuttleView::ShowFace() { SelectPage(face_page_); }

void RobotInfoShuttleView::ShowSchedule(const UpcomingScheduleList& schedules) {
    StopScheduleScroll();
    schedule_rows_.clear();
    lv_obj_clean(schedule_list_);
    lv_obj_scroll_to_y(schedule_list_, 0, LV_ANIM_OFF);

    const auto& palette = Palette(calendar_theme_);
    const int row_width = width_ - 32;
    auto add_row = [&](const std::string& when, const std::string& title, bool placeholder) {
        const int title_width = row_width - 24;
        const lv_font_t* title_font = placeholder ? &title_font_ : &robot_info_bold_20;
        lv_point_t title_size{};
        lv_text_get_size(&title_size, title.c_str(), title_font, 0, 2, title_width,
                         LV_TEXT_FLAG_NONE);
        const int title_height = std::max<int>(title_size.y, 24);
        const int row_height = std::max(62, placeholder ? title_height + 24 : title_height + 35);

        auto* row = lv_obj_create(schedule_list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, row_width, row_height);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        Shape(row, 0, 9, 3, row_height - 18, palette.schedule_accent, 2);

        if (placeholder) {
            Label(row, 14, (row_height - title_height) / 2, title_width, title_height + 4,
                  title.c_str(), 0xD1D1D6, title_font, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_WRAP);
        } else {
            Label(row, 14, 5, title_width, 18, when.c_str(), palette.schedule_accent, &text_font_);
            Label(row, 14, 24, title_width, title_height + 4, title.c_str(), 0xF2F2F7, title_font,
                  LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_WRAP);
        }
        schedule_rows_.push_back(row);
    };

    if (!schedules.clock_ready) {
        add_row("", "Đang đồng bộ thời gian", true);
    } else if (schedules.events.empty()) {
        add_row("", "Chưa có lịch sắp tới", true);
    } else {
        for (const auto& event : schedules.events)
            add_row(event.date + " · " + event.time, event.title, false);
    }

    SelectPage(schedule_page_);
}

void RobotInfoShuttleView::StartScheduleScroll() {
    if (schedule_scroll_timer_ != nullptr || schedule_list_ == nullptr || schedule_rows_.size() < 2)
        return;

    lv_obj_update_layout(schedule_list_);
    const int32_t viewport_height = lv_obj_get_height(schedule_list_);
    schedule_first_scroll_index_ = 0;
    for (auto* row : schedule_rows_) {
        if (lv_obj_get_y(row) + lv_obj_get_height(row) > viewport_height)
            break;
        ++schedule_first_scroll_index_;
    }
    if (schedule_first_scroll_index_ >= schedule_rows_.size() ||
        lv_obj_get_scroll_bottom(schedule_list_) <= 0)
        return;

    schedule_scroll_index_ = schedule_first_scroll_index_;
    schedule_scroll_timer_ = lv_timer_create(OnScheduleScrollTimer, 3500, this);
}

void RobotInfoShuttleView::StopScheduleScroll() {
    if (schedule_scroll_timer_ != nullptr) {
        lv_timer_delete(schedule_scroll_timer_);
        schedule_scroll_timer_ = nullptr;
    }
}

void RobotInfoShuttleView::OnScheduleScrollTimer(lv_timer_t* timer) {
    auto* self = static_cast<RobotInfoShuttleView*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->root_ == nullptr || self->schedule_list_ == nullptr ||
        self->schedule_page_ == nullptr || lv_obj_has_flag(self->root_, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(self->schedule_page_, LV_OBJ_FLAG_HIDDEN)) {
        if (self != nullptr)
            self->StopScheduleScroll();
        return;
    }

    if (self->schedule_scroll_index_ >= self->schedule_rows_.size()) {
        lv_obj_scroll_to_y(self->schedule_list_, 0, LV_ANIM_ON);
        self->schedule_scroll_index_ = self->schedule_first_scroll_index_;
        return;
    }
    lv_obj_scroll_to_view(self->schedule_rows_[self->schedule_scroll_index_++], LV_ANIM_ON);
}

void RobotInfoShuttleView::ShowWeather(const std::string& summary,
                                       const std::string& configured_location, int icon_kind) {
    std::string temperature;
    std::string location;
    std::string condition;
    if (!summary.empty()) {
        size_t unit = summary.find("°C");
        size_t unit_size = 3;
        if (unit == std::string::npos) {
            unit = summary.find("°F");
        }
        if (unit != std::string::npos) {
            size_t start = unit;
            while (start > 0 && (std::isdigit(static_cast<unsigned char>(summary[start - 1])) ||
                                 summary[start - 1] == '-' || summary[start - 1] == '.')) {
                --start;
            }
            if (start < unit) {
                temperature = summary.substr(start, unit + unit_size - start);
                location = summary.substr(0, start);
                condition = summary.substr(unit + unit_size);
                Trim(location);
                Trim(condition);
            }
        }
        if (temperature.empty())
            condition = summary;
    } else {
        location = configured_location;
    }
    if (location.empty())
        location = configured_location;

    lv_label_set_text(weather_temperature_, temperature.empty() ? "--°" : temperature.c_str());
    lv_label_set_text(weather_location_, location.empty() ? "DỰ BÁO THỜI TIẾT" : location.c_str());
    lv_label_set_text(weather_condition_,
                      condition.empty() ? "Chưa nhận dữ liệu thời tiết" : condition.c_str());
    lv_label_set_text(weather_footer_, summary.empty() ? "Đang chờ data" : "Latest update");
    lv_obj_set_style_text_font(weather_location_, &robot_info_bold_20, 0);
    lv_obj_set_style_text_font(weather_temperature_, &robot_info_bold_36, 0);
    float degrees = std::strtof(temperature.c_str(), nullptr);
    if (temperature.find("°F") != std::string::npos)
        degrees = (degrees - 32) * 5 / 9;
    lv_obj_set_style_text_color(weather_temperature_,
                                lv_color_hex(temperature.empty() ? 0xFFFFFF
                                             : degrees < 30      ? 0x30D158
                                             : degrees < 34      ? 0xFF9F0A
                                                                 : 0xFF453A),
                                0);
    SetWeatherIcon(icon_kind);
    SelectPage(weather_page_);
}

void RobotInfoShuttleView::SetWeatherIcon(int icon_kind) {
    const int kind = icon_kind >= 0 && icon_kind <= 6 ? icon_kind : 1;
    if (kind != weather_icon_kind_)
        StopWeatherAnimation();
    weather_icon_kind_ = kind;
    for (auto* icon :
         {weather_icon_sun_, weather_icon_cloud_, weather_icon_storm_, weather_icon_rain_group_,
          weather_icon_wind_group_, weather_icon_snow_, weather_icon_fog_}) {
        if (icon != nullptr)
            lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t* selected = weather_icon_cloud_;
    switch (kind) {
        case 0:
            selected = weather_icon_sun_;
            break;
        case 2:
            selected = weather_icon_rain_group_;
            break;
        case 3:
            selected = weather_icon_storm_;
            break;
        case 4:
            selected = weather_icon_wind_group_;
            break;
        case 5:
            selected = weather_icon_snow_;
            break;
        case 6:
            selected = weather_icon_fog_;
            break;
        default:
            break;
    }
    if (selected != nullptr)
        lv_obj_remove_flag(selected, LV_OBJ_FLAG_HIDDEN);
}

void RobotInfoShuttleView::StartWeatherAnimation() {
    if (weather_animation_active_ || (weather_icon_kind_ != 2 && weather_icon_kind_ != 3))
        return;
    lv_obj_t** drops = weather_icon_kind_ == 3 ? weather_icon_storm_rain_ : weather_icon_rain_;
    for (int i = 0; i < 3; ++i) {
        if (drops[i] == nullptr)
            continue;
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, drops[i]);
        lv_anim_set_exec_cb(&animation, [](void* object, int32_t y) {
            lv_obj_set_y(static_cast<lv_obj_t*>(object), y);
        });
        lv_anim_set_values(&animation, 41, 46);
        lv_anim_set_duration(&animation, 600);
        lv_anim_set_playback_duration(&animation, 600);
        lv_anim_set_delay(&animation, static_cast<uint32_t>(i * 170));
        lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&animation);
    }
    if (weather_icon_kind_ == 3 && weather_icon_flash_ != nullptr) {
        lv_anim_t flash;
        lv_anim_init(&flash);
        lv_anim_set_var(&flash, weather_icon_flash_);
        lv_anim_set_exec_cb(&flash, [](void* object, int32_t opacity) {
            lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(object), static_cast<lv_opa_t>(opacity),
                                    0);
        });
        lv_anim_set_values(&flash, LV_OPA_40, LV_OPA_COVER);
        lv_anim_set_duration(&flash, 450);
        lv_anim_set_playback_duration(&flash, 450);
        lv_anim_set_delay(&flash, 300);
        lv_anim_set_repeat_count(&flash, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&flash);
    }
    weather_animation_active_ = true;
}

void RobotInfoShuttleView::StopWeatherAnimation() {
    if (!weather_animation_active_)
        return;
    for (auto* drop : weather_icon_rain_) {
        if (drop != nullptr)
            lv_anim_delete(drop, nullptr);
    }
    for (auto* drop : weather_icon_storm_rain_) {
        if (drop != nullptr)
            lv_anim_delete(drop, nullptr);
    }
    if (weather_icon_flash_ != nullptr)
        lv_anim_delete(weather_icon_flash_, nullptr);
    weather_animation_active_ = false;
}

void RobotInfoShuttleView::SetVisible(bool visible) {
    if (root_ == nullptr)
        return;
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        if (!lv_obj_has_flag(weather_page_, LV_OBJ_FLAG_HIDDEN))
            StartWeatherAnimation();
        if (!lv_obj_has_flag(schedule_page_, LV_OBJ_FLAG_HIDDEN))
            StartScheduleScroll();
    } else {
        StopScheduleScroll();
        StopWeatherAnimation();
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace kynangso
