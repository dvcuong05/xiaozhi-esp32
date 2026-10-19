#ifndef MRKENDDO_ROBOT_EYES_H_
#define MRKENDDO_ROBOT_EYES_H_

#include <lvgl.h>
#include "robot_face_art.h"

#include <cstdint>

// Procedural face inspired by the QGIF samples, with an independent audio mouth.
class RobotEyesView {
public:
    RobotEyesView(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
    ~RobotEyesView();

    RobotEyesView(const RobotEyesView&) = delete;
    RobotEyesView& operator=(const RobotEyesView&) = delete;

    bool SetEmotion(const char* emotion);
    void SetStatus(RobotEyesMode mode);
    void SetVisible(bool visible);
    void SetAnimation(bool enabled);
    void SetSpeechLevel(uint8_t level);
    void SetSize(lv_coord_t width, lv_coord_t height);

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* mouth_inside_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_coord_t width_ = 0;
    lv_coord_t height_ = 0;
    RobotEyesMode mode_ = RobotEyesMode::Neutral;
    RobotEyesMode status_mode_ = RobotEyesMode::Neutral;
    uint32_t frame_ = 0;
    uint8_t speech_target_ = 0;
    uint8_t speech_level_ = 0;
    uint32_t speech_updated_ms_ = 0;
    bool animation_enabled_ = true;
    bool visible_ = true;

    static void OnTimer(lv_timer_t* timer);
    void Render();
    static void DrawFace(lv_event_t* event);
    void RenderMouth();
    void SetMode(RobotEyesMode mode);
    static bool MapEmotion(const char* emotion, RobotEyesMode& mode);
};

#endif  // MRKENDDO_ROBOT_EYES_H_
