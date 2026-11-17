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
    static constexpr size_t kFireworkCount = 16;

    int width_;
    int height_;
    int wheel_diameter_ = 0;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* wheel_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* pointer_ = nullptr;
    lv_obj_t* result_ = nullptr;
    std::vector<lv_obj_t*> slice_labels_;
    lv_obj_t* fireworks_[kFireworkCount] = {};
    lv_timer_t* timer_ = nullptr;
    std::vector<std::string> names_;
    std::string winner_;
    std::function<void()> on_finished_;
    lv_font_t text_font_{};
    uint32_t started_at_ = 0;
    size_t winner_index_ = 0;
    float wheel_rotation_ = 0.0f;
    float spin_start_rotation_ = 0.0f;
    float spin_target_rotation_ = 0.0f;
    const lv_font_t* fallback_font_ = nullptr;
    bool active_ = false;
    bool revealed_ = false;

    static void DrawWheel(lv_event_t* event);
    static void DrawPointer(lv_event_t* event);
    static void OnTimer(lv_timer_t* timer);
    void Tick();
    void RevealWinner();
    void UpdateSliceLabels();
    void UpdateFireworks(uint32_t elapsed);
};

}  // namespace kynangso

#endif  // KYNANGSO_ROBOT_NAME_WHEEL_VIEW_H_
