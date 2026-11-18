#pragma once

#include <lvgl.h>

#include "features/time/vietnamese_lunar_calendar.h"

namespace kynangso {
class RobotLunarCalendarView {
public:
    RobotLunarCalendarView(int width, int height, const lv_font_t* font);
    ~RobotLunarCalendarView();

    void Show(int solar_day, int solar_month, int solar_year, const VietnameseLunarDate& lunar);
    void SetVisible(bool visible);
    void SetFont(const lv_font_t* font);

private:
    lv_font_t text_font_{};
    lv_obj_t* root_ = nullptr;
    lv_obj_t* day_ = nullptr;
    lv_obj_t* month_ = nullptr;
    lv_obj_t* leap_ = nullptr;
    lv_obj_t* solar_ = nullptr;
};
}  // namespace kynangso
