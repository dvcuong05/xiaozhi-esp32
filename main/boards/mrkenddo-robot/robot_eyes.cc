#include "robot_eyes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kTimerPeriodMs = 40;
constexpr uint32_t kBootOpenFrames = 36;
constexpr int kClosedEyeHeight = 4;
constexpr uint32_t kBlinkCycleFrames = 170;
constexpr uint32_t kBlinkDurationFrames = 10;
constexpr uint32_t kNeutralLookCycleFrames = 240;
constexpr int kNeutralLookOffset = 10;
constexpr int kNeutralCuriosityHeight = 12;
constexpr uint32_t kRobotCyan = 0x0ABDE3;

int Clamp(int value, int minimum, int maximum) {
    return std::max(minimum, std::min(value, maximum));
}

float SmoothStep(float progress) {
    progress = std::max(0.0f, std::min(progress, 1.0f));
    return progress * progress * (3.0f - 2.0f * progress);
}

float NeutralLookPosition(uint32_t frame) {
    // Move, pause, and return instead of continuously oscillating. This makes
    // the face look like it is casually observing its surroundings.
    constexpr uint32_t kMoveFrames = 28;
    constexpr uint32_t kHoldFrames = 42;
    constexpr uint32_t kCenterHoldFrames = 24;
    const uint32_t phase = frame % kNeutralLookCycleFrames;

    if (phase < kMoveFrames) {
        return -SmoothStep(static_cast<float>(phase) / kMoveFrames);
    }
    if (phase < kMoveFrames + kHoldFrames) {
        return -1.0f;
    }
    if (phase < kMoveFrames * 2 + kHoldFrames) {
        const uint32_t offset = phase - kMoveFrames - kHoldFrames;
        return -1.0f + SmoothStep(static_cast<float>(offset) / kMoveFrames);
    }
    if (phase < kMoveFrames * 2 + kHoldFrames + kCenterHoldFrames) {
        return 0.0f;
    }

    const uint32_t right_start = kMoveFrames * 2 + kHoldFrames + kCenterHoldFrames;
    if (phase < right_start + kMoveFrames) {
        const uint32_t offset = phase - right_start;
        return SmoothStep(static_cast<float>(offset) / kMoveFrames);
    }
    if (phase < right_start + kMoveFrames + kHoldFrames) {
        return 1.0f;
    }

    const uint32_t offset = phase - right_start - kMoveFrames - kHoldFrames;
    return 1.0f - SmoothStep(static_cast<float>(offset) / kMoveFrames);
}

int NeutralCuriosityHeight(uint32_t frame) {
    // Briefly widen the eyes after a glance, like a small moment of curiosity.
    constexpr uint32_t kCuriosityStart = 92;
    constexpr uint32_t kCuriosityFrames = 28;
    const uint32_t phase = frame % kNeutralLookCycleFrames;
    if (phase < kCuriosityStart || phase >= kCuriosityStart + kCuriosityFrames) {
        return 0;
    }

    const float progress = static_cast<float>(phase - kCuriosityStart) / kCuriosityFrames;
    return static_cast<int>(std::sin(progress * 3.14159265f) * kNeutralCuriosityHeight);
}

}  // namespace

RobotEyesView::RobotEyesView(lv_obj_t* parent, lv_coord_t width, lv_coord_t height)
    : width_(width), height_(height) {
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, width_, height_);
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    left_eye_ = lv_obj_create(root_);
    right_eye_ = lv_obj_create(root_);

    for (lv_obj_t* eye : {left_eye_, right_eye_}) {
        lv_obj_set_style_bg_color(eye, lv_color_hex(kRobotCyan), 0);
        lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(eye, 0, 0);
        lv_obj_set_style_pad_all(eye, 0, 0);
        lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
    }

    mouth_ = lv_obj_create(root_);
    mouth_inside_ = lv_obj_create(mouth_);
    for (lv_obj_t* object : {mouth_, mouth_inside_}) {
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(object, 0, 0);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_style_bg_color(mouth_, lv_color_hex(kRobotCyan), 0);
    lv_obj_set_style_bg_color(mouth_inside_, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);

    timer_ = lv_timer_create(OnTimer, kTimerPeriodMs, this);
    Render();
}

RobotEyesView::~RobotEyesView() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void RobotEyesView::OnTimer(lv_timer_t* timer) {
    auto* view = static_cast<RobotEyesView*>(lv_timer_get_user_data(timer));
    if (view == nullptr) {
        return;
    }
    ++view->frame_;
    view->Render();
}

void RobotEyesView::SetVisible(bool visible) {
    if (root_ == nullptr) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RobotEyesView::SetMode(RobotEyesMode mode) {
    mode_ = mode;
    frame_ = 0;
    Render();
}

void RobotEyesView::SetSize(lv_coord_t width, lv_coord_t height) {
    width_ = width;
    height_ = height;
    lv_obj_set_size(root_, width_, height_);
    lv_obj_center(root_);
    Render();
}

void RobotEyesView::SetStatus(RobotEyesMode mode) {
    if (mode != RobotEyesMode::Speaking || status_mode_ != RobotEyesMode::Speaking) {
        SetSpeechLevel(0);
    }
    status_mode_ = mode;
    SetMode(mode);
}

void RobotEyesView::SetSpeechLevel(uint8_t level) {
    speech_target_ = level;
    speech_updated_ms_ = lv_tick_get();
    if (level == 0) {
        speech_level_ = 0;
    }
}

void RobotEyesView::RenderMouth() {
    // Emotions may alter the eyes during TTS; the speaking state independently
    // owns the mouth, so a "happy" event cannot accidentally stop lip motion.
    if (status_mode_ != RobotEyesMode::Speaking) {
        lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
    // Close during pauses/underruns, even if the server has not sent TTS stop.
    if (static_cast<uint32_t>(lv_tick_get() - speech_updated_ms_) > 160) {
        speech_target_ = 0;
    }
    const int target = speech_target_;
    speech_level_ = target > speech_level_ ? (target + speech_level_) / 2
                                           : std::max(target, static_cast<int>(speech_level_) - 36);
    const int mouth_height = 4 + speech_level_ * 26 / 255;
    const int mouth_width = std::max(40, static_cast<int>(width_) / 3) - speech_level_ / 24;
    lv_obj_set_size(mouth_, mouth_width, mouth_height);
    lv_obj_set_pos(mouth_, (width_ - mouth_width) / 2, height_ * 4 / 5 - mouth_height / 2);
    lv_obj_set_style_radius(mouth_, mouth_height / 2, 0);
    if (mouth_height >= 10) {
        lv_obj_remove_flag(mouth_inside_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mouth_inside_, mouth_width - 8, mouth_height - 6);
        lv_obj_center(mouth_inside_);
        lv_obj_set_style_radius(mouth_inside_, mouth_height / 2, 0);
    } else {
        lv_obj_add_flag(mouth_inside_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool RobotEyesView::MapEmotion(const char* emotion, RobotEyesMode& mode) {
    if (emotion == nullptr || emotion[0] == '\0') {
        return false;
    }

    if (std::strcmp(emotion, "neutral") == 0 || std::strcmp(emotion, "robot_2") == 0) {
        mode = std::strcmp(emotion, "robot_2") == 0 ? RobotEyesMode::Boot : RobotEyesMode::Neutral;
        return true;
    }
    if (std::strcmp(emotion, "happy") == 0 || std::strcmp(emotion, "smile") == 0 ||
        std::strcmp(emotion, "laugh") == 0 || std::strcmp(emotion, "joy") == 0) {
        mode = RobotEyesMode::Happy;
        return true;
    }
    if (std::strcmp(emotion, "surprised") == 0 || std::strcmp(emotion, "astonished") == 0 ||
        std::strcmp(emotion, "wow") == 0 || std::strcmp(emotion, "panic") == 0) {
        mode = RobotEyesMode::Surprised;
        return true;
    }
    if (std::strcmp(emotion, "sad") == 0 || std::strcmp(emotion, "cry") == 0 ||
        std::strcmp(emotion, "disappointed") == 0) {
        mode = RobotEyesMode::Sad;
        return true;
    }
    if (std::strcmp(emotion, "angry") == 0 || std::strcmp(emotion, "anger") == 0) {
        mode = RobotEyesMode::Angry;
        return true;
    }
    if (std::strcmp(emotion, "sleepy") == 0 || std::strcmp(emotion, "sleep") == 0) {
        mode = RobotEyesMode::Sleepy;
        return true;
    }
    if (std::strcmp(emotion, "confused") == 0 || std::strcmp(emotion, "thinking") == 0) {
        mode = RobotEyesMode::Confused;
        return true;
    }
    if (std::strcmp(emotion, "listening") == 0) {
        mode = RobotEyesMode::Listening;
        return true;
    }
    if (std::strcmp(emotion, "speaking") == 0) {
        mode = RobotEyesMode::Speaking;
        return true;
    }
    return false;
}

bool RobotEyesView::SetEmotion(const char* emotion) {
    RobotEyesMode mode;
    if (!MapEmotion(emotion, mode)) {
        return false;
    }

    // "neutral" means follow the current application state. This preserves the
    // listening/speaking animation when Application resets the emotion to neutral.
    if (mode == RobotEyesMode::Neutral) {
        SetMode(status_mode_);
    } else {
        SetMode(mode);
    }
    return true;
}

void RobotEyesView::RenderEye(lv_obj_t* eye, const EyeGeometry& geometry) {
    lv_obj_set_pos(eye, geometry.x, geometry.y);
    lv_obj_set_size(eye, geometry.width, geometry.height);
    const int eye_radius = std::max(8, std::min(18, std::min(geometry.width, geometry.height) / 5));
    lv_obj_set_style_radius(eye, eye_radius, 0);
}

void RobotEyesView::Render() {
    if (root_ == nullptr) {
        return;
    }

    const int center_y = static_cast<int>(height_) / 2;
    const int eye_gap = std::max(12, static_cast<int>(width_) / 16);
    const int normal_width = std::max(42, static_cast<int>(width_) / 3);
    const int normal_height = std::max(48, static_cast<int>(height_) * 3 / 8);
    int y = center_y - normal_height / 2;
    int eye_width = normal_width;
    int eye_height = normal_height;
    int right_y = y;
    int gaze_x = 0;
    int gaze_y = 0;

    bool apply_blink = true;
    switch (mode_) {
        case RobotEyesMode::Boot: {
            // Start as two thin cyan slits at the center and smoothly reveal
            // the full rounded eye panels as if the robot is waking up.
            const float progress = std::min(1.0f, static_cast<float>(frame_) / kBootOpenFrames);
            const float eased = progress * progress * (3.0f - 2.0f * progress);
            const int opening = Clamp(
                kClosedEyeHeight + static_cast<int>((normal_height - kClosedEyeHeight) * eased),
                kClosedEyeHeight, normal_height);
            eye_height = opening;
            y = center_y - opening / 2;
            right_y = y;
            apply_blink = false;
            break;
        }
        case RobotEyesMode::Listening:
            eye_height += static_cast<int>(std::sin(frame_ * 0.11f) * 2);
            break;
        case RobotEyesMode::Speaking:
            eye_height += static_cast<int>((frame_ / 2) % 3) * 3;
            break;
        case RobotEyesMode::Happy:
            eye_height = normal_height / 2;
            y = center_y - eye_height / 2 + 12;
            break;
        case RobotEyesMode::Surprised:
            eye_width = normal_width + 8;
            eye_height = normal_height + 20;
            y = center_y - eye_height / 2;
            break;
        case RobotEyesMode::Sad:
            eye_height = normal_height * 2 / 3;
            y = center_y - eye_height / 2 + 8;
            break;
        case RobotEyesMode::Angry:
            eye_height = normal_height * 2 / 3;
            y = center_y - eye_height / 2;
            right_y += 10;
            break;
        case RobotEyesMode::Sleepy:
            eye_height = 14;
            y = center_y - eye_height / 2 + 12;
            break;
        case RobotEyesMode::Confused:
            eye_height = normal_height * 3 / 4;
            y = center_y - eye_height / 2;
            break;
        case RobotEyesMode::Neutral:
            gaze_x = static_cast<int>(NeutralLookPosition(frame_) * kNeutralLookOffset);
            gaze_y = static_cast<int>(std::sin(frame_ * 0.018f) * 2.0f);
            eye_height += NeutralCuriosityHeight(frame_);
            y += gaze_y;
            right_y = y;
            break;
    }

    if (apply_blink) {
        const uint32_t blink_frame = frame_ % kBlinkCycleFrames;
        if (blink_frame < kBlinkDurationFrames) {
            const int blink_height = blink_frame < 3 || blink_frame >= 7
                                         ? std::max(kClosedEyeHeight, eye_height / 3)
                                         : kClosedEyeHeight;
            eye_height = blink_height;
            y = center_y + gaze_y - blink_height / 2;
            if (mode_ != RobotEyesMode::Angry) {
                right_y = y;
            }
        }
    }

    const int adjusted_left_x = (static_cast<int>(width_) - eye_width * 2 - eye_gap) / 2 + gaze_x;
    const int adjusted_right_x = adjusted_left_x + eye_width + eye_gap;
    if (status_mode_ == RobotEyesMode::Speaking) {
        y -= 12;
        right_y -= 12;
    }
    const EyeGeometry left{adjusted_left_x, y, eye_width, eye_height};
    const EyeGeometry right{adjusted_right_x, right_y, eye_width, eye_height};
    RenderEye(left_eye_, left);
    RenderEye(right_eye_, right);
    RenderMouth();
}
