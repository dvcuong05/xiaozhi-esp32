#include "robot_eyes.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr int kTimerPeriodMs = 40;

class FaceCanvas {
public:
    FaceCanvas(lv_layer_t* layer, const lv_area_t& bounds)
        : layer_(layer),
          bounds_(bounds),
          sx_(lv_area_get_width(&bounds) / 240.0f),
          sy_(lv_area_get_height(&bounds) / 144.0f) {}

    void Line(float x1, float y1, float x2, float y2, float width, unsigned color) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.p1.x = bounds_.x1 + static_cast<int>(x1 * sx_);
        dsc.p1.y = bounds_.y1 + static_cast<int>(y1 * sy_);
        dsc.p2.x = bounds_.x1 + static_cast<int>(x2 * sx_);
        dsc.p2.y = bounds_.y1 + static_cast<int>(y2 * sy_);
        dsc.width = std::max(1, static_cast<int>(width * std::min(sx_, sy_)));
        dsc.color = lv_color_hex(color);
        dsc.round_start = dsc.round_end = true;
        lv_draw_line(layer_, &dsc);
    }

    void Box(float x, float y, float width, float height, float radius, unsigned color) {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(color);
        dsc.bg_opa = LV_OPA_COVER;
        dsc.radius = static_cast<int>(radius * std::min(sx_, sy_));
        const lv_area_t area = {
            bounds_.x1 + static_cast<int>(x * sx_),
            bounds_.y1 + static_cast<int>(y * sy_),
            bounds_.x1 + static_cast<int>((x + width) * sx_) - 1,
            bounds_.y1 + static_cast<int>((y + height) * sy_) - 1,
        };
        lv_draw_rect(layer_, &dsc, &area);
    }

    void Triangle(float x1, float y1, float x2, float y2, float x3, float y3, unsigned color) {
        lv_draw_triangle_dsc_t dsc;
        lv_draw_triangle_dsc_init(&dsc);
        const float x[] = {x1, x2, x3};
        const float y[] = {y1, y2, y3};
        for (int i = 0; i < 3; ++i) {
            dsc.p[i].x = bounds_.x1 + static_cast<int>(x[i] * sx_);
            dsc.p[i].y = bounds_.y1 + static_cast<int>(y[i] * sy_);
        }
        dsc.color = lv_color_hex(color);
        dsc.opa = LV_OPA_COVER;
        lv_draw_triangle(layer_, &dsc);
    }

private:
    lv_layer_t* layer_;
    lv_area_t bounds_;
    float sx_;
    float sy_;
};
}  // namespace

RobotEyesView::RobotEyesView(lv_obj_t* parent, lv_coord_t width, lv_coord_t height)
    : width_(width), height_(height) {
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, width_, height_);
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root_, DrawFace, LV_EVENT_DRAW_MAIN, this);

    mouth_ = lv_obj_create(root_);
    mouth_inside_ = lv_obj_create(mouth_);
    for (lv_obj_t* object : {mouth_, mouth_inside_}) {
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(object, 0, 0);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_style_bg_color(mouth_, lv_color_hex(mrkenddo::face_art::kCyan), 0);
    lv_obj_set_style_bg_color(mouth_inside_, lv_color_black(), 0);
    lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
    timer_ = lv_timer_create(OnTimer, kTimerPeriodMs, this);
    Render();
}

RobotEyesView::~RobotEyesView() {
    if (timer_ != nullptr)
        lv_timer_delete(timer_);
    if (root_ != nullptr)
        lv_obj_delete(root_);
}

void RobotEyesView::DrawFace(lv_event_t* event) {
    auto* view = static_cast<RobotEyesView*>(lv_event_get_user_data(event));
    lv_area_t bounds;
    lv_obj_get_coords(view->root_, &bounds);
    // Leave room for the independent audio mouth, including wide surprise eyes.
    if (view->status_mode_ == RobotEyesMode::Speaking) {
        const int offset = lv_area_get_height(&bounds) / 24;
        bounds.y1 -= offset;
        bounds.y2 -= offset;
    }
    FaceCanvas canvas(lv_event_get_layer(event), bounds);
    const unsigned frame =
        view->animation_enabled_ ? view->frame_ : (view->mode_ == RobotEyesMode::Boot ? 36 : 40);
    mrkenddo::face_art::Draw(canvas, view->mode_, frame,
                             view->status_mode_ == RobotEyesMode::Speaking);
}

void RobotEyesView::OnTimer(lv_timer_t* timer) {
    auto* view = static_cast<RobotEyesView*>(lv_timer_get_user_data(timer));
    ++view->frame_;
    view->Render();
}

void RobotEyesView::SetVisible(bool visible) {
    visible_ = visible;
    if (visible)
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    SetAnimation(animation_enabled_);
}

void RobotEyesView::SetAnimation(bool enabled) {
    animation_enabled_ = enabled;
    if (timer_ != nullptr) {
        if (enabled && visible_)
            lv_timer_resume(timer_);
        else
            lv_timer_pause(timer_);
    }
    Render();
}

void RobotEyesView::SetMode(RobotEyesMode mode) {
    // Repeated server packets must not restart the same expressive gesture.
    if (mode_ != mode) {
        mode_ = mode;
        frame_ = 0;
    }
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
    if (mode != RobotEyesMode::Speaking || status_mode_ != RobotEyesMode::Speaking)
        SetSpeechLevel(0);
    status_mode_ = mode;
    SetMode(mode);
}

void RobotEyesView::SetSpeechLevel(uint8_t level) {
    speech_target_ = level;
    speech_updated_ms_ = lv_tick_get();
    if (level == 0)
        speech_level_ = 0;
}

void RobotEyesView::RenderMouth() {
    if (status_mode_ != RobotEyesMode::Speaking) {
        lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
    if (static_cast<uint32_t>(lv_tick_get() - speech_updated_ms_) > 160)
        speech_target_ = 0;
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
    if (emotion == nullptr || emotion[0] == '\0')
        return false;
    struct Alias {
        const char* name;
        RobotEyesMode mode;
    };
    static constexpr Alias aliases[] = {
        {"neutral", RobotEyesMode::Neutral},
        {"robot_2", RobotEyesMode::Boot},
        {"happy", RobotEyesMode::Happy},
        {"smile", RobotEyesMode::Happy},
        {"laugh", RobotEyesMode::Laugh},
        {"joy", RobotEyesMode::Laugh},
        {"surprised", RobotEyesMode::Surprised},
        {"astonished", RobotEyesMode::Surprised},
        {"wow", RobotEyesMode::Surprised},
        {"panic", RobotEyesMode::Surprised},
        {"sad", RobotEyesMode::Sad},
        {"cry", RobotEyesMode::Sad},
        {"crying", RobotEyesMode::Sad},
        {"disappointed", RobotEyesMode::Sad},
        {"angry", RobotEyesMode::Angry},
        {"anger", RobotEyesMode::Angry},
        {"sleepy", RobotEyesMode::Sleepy},
        {"sleep", RobotEyesMode::Sleepy},
        {"yawn", RobotEyesMode::Yawn},
        {"action_yawn", RobotEyesMode::Yawn},
        {"confused", RobotEyesMode::Confused},
        {"thinking", RobotEyesMode::Confused},
        {"distracted", RobotEyesMode::Distracted},
        {"dizzy", RobotEyesMode::Dizzy},
        {"love", RobotEyesMode::Love},
        {"loving", RobotEyesMode::Love},
        {"heart", RobotEyesMode::Love},
        {"devil", RobotEyesMode::Devil},
        {"mischievous", RobotEyesMode::Devil},
        {"confident", RobotEyesMode::Confident},
        {"cool", RobotEyesMode::Confident},
        {"playful", RobotEyesMode::Playful},
        {"wink", RobotEyesMode::Playful},
        {"listening", RobotEyesMode::Listening},
        {"speaking", RobotEyesMode::Speaking},
    };
    for (const auto& alias : aliases) {
        if (std::strcmp(emotion, alias.name) == 0) {
            mode = alias.mode;
            return true;
        }
    }
    return false;
}

bool RobotEyesView::SetEmotion(const char* emotion) {
    RobotEyesMode mode;
    if (!MapEmotion(emotion, mode))
        return false;
    SetMode(mode == RobotEyesMode::Neutral ? status_mode_ : mode);
    return true;
}

void RobotEyesView::Render() {
    RenderMouth();
    lv_obj_invalidate(root_);
}
