#pragma once
#include <lvgl.h>
#include "robot_time_service.h"
namespace mrkenddo {
class RobotTimeView {
public:
    RobotTimeView(int width, int height, const lv_font_t* font);
    ~RobotTimeView();
    void Update(const TimeView& view);
    void SetVisible(bool visible);
    void SetFont(const lv_font_t* font);

private:
    // Static Latin/Vietnamese glyphs, followed by the current theme for other scripts.
    lv_font_t text_font_{};
    lv_obj_t *root_, *heading_, *title_, *footer_, *ring_, *digits_;
    std::string number_;
    uint32_t accent_ = 0xA78BFA;
    static void DrawDigits(lv_event_t* event);
};
}  // namespace mrkenddo
