#ifndef KYNANGSO_ROBOT_NAME_WHEEL_VIEW_H_
#define KYNANGSO_ROBOT_NAME_WHEEL_VIEW_H_

#include <lvgl.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kynangso {

class RobotNameWheelView {
public:
    RobotNameWheelView(int width, int height, const lv_font_t* font,
                       std::function<void()> on_finished);
    ~RobotNameWheelView();

    bool Start(std::vector<std::string> names, std::string winner);
    void SetVisible(bool visible);
    void SetFont(const lv_font_t* font);
    bool IsActive() const { return active_; }

private:
    int width_;
    int height_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* ring_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* pointer_ = nullptr;
    lv_obj_t* name_ = nullptr;
    lv_obj_t* result_ = nullptr;
    lv_obj_t* confetti_[8] = {};
    lv_timer_t* timer_ = nullptr;
    std::vector<std::string> names_;
    std::string winner_;
    std::function<void()> on_finished_;
    uint32_t started_at_ = 0;
    uint32_t last_change_ = 0;
    size_t current_index_ = 0;
    bool active_ = false;
    bool revealed_ = false;

    static void OnTimer(lv_timer_t* timer);
    void Tick();
    void RevealWinner();
};

}  // namespace kynangso

#endif  // KYNANGSO_ROBOT_NAME_WHEEL_VIEW_H_
