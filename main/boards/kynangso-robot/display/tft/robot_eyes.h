#ifndef KYNANGSO_ROBOT_EYES_H_
#define KYNANGSO_ROBOT_EYES_H_

#include <lvgl.h>
#include "display/tft/robot_face_art.h"
#include "features/face/face_theme.h"

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
    void SetTheme(const kynangso::FaceTheme& theme);

private:
    lv_obj_t* root_ = nullptr;
    kynangso::FaceTheme theme_;
    lv_obj_t* mouth_ = nullptr;
#if CONFIG_LV_USE_GIF
    lv_obj_t* gif_ = nullptr;
    lv_image_dsc_t gif_idle_{};
    lv_image_dsc_t gif_talking_{};
    lv_image_dsc_t gif_single_{};
    bool gif_loaded_ = false;
    bool gif_talking_active_ = false;
    bool gif_single_active_ = false;
    void UpdateGif();
#endif
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

#endif  // KYNANGSO_ROBOT_EYES_H_
