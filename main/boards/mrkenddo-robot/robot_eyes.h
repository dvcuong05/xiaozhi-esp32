#ifndef MRKENDDO_ROBOT_EYES_H_
#define MRKENDDO_ROBOT_EYES_H_

#include <lvgl.h>

#include <cstdint>

enum class RobotEyesMode : uint8_t {
    Neutral,
    Listening,
    Speaking,
    Happy,
    Surprised,
    Sad,
    Angry,
    Sleepy,
    Confused,
    Boot,
};

// Board-local vector face for the MrkenDDo Robot TFT. The face intentionally
// uses solid cyan rounded panels without pupils so it remains crisp after any
// panel rotation and keeps the display asset footprint small.
class RobotEyesView {
public:
    RobotEyesView(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
    ~RobotEyesView();

    RobotEyesView(const RobotEyesView&) = delete;
    RobotEyesView& operator=(const RobotEyesView&) = delete;

    bool SetEmotion(const char* emotion);
    void SetStatus(RobotEyesMode mode);
    void SetVisible(bool visible);

private:
    struct EyeGeometry {
        int x;
        int y;
        int width;
        int height;
    };

    lv_obj_t* root_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_coord_t width_ = 0;
    lv_coord_t height_ = 0;
    RobotEyesMode mode_ = RobotEyesMode::Neutral;
    RobotEyesMode status_mode_ = RobotEyesMode::Neutral;
    uint32_t frame_ = 0;

    static void OnTimer(lv_timer_t* timer);
    void Render();
    void RenderEye(lv_obj_t* eye, const EyeGeometry& geometry);
    void SetMode(RobotEyesMode mode);
    static bool MapEmotion(const char* emotion, RobotEyesMode& mode);
};

#endif  // MRKENDDO_ROBOT_EYES_H_
