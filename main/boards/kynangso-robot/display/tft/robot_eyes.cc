#include "display/tft/robot_eyes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int kTimerPeriodMs = 40;

class FaceCanvas {
public:
    FaceCanvas(lv_layer_t* layer, const lv_area_t& bounds, const kynangso::FaceTheme& theme,
               unsigned frame)
        : layer_(layer),
          bounds_(bounds),
          theme_(theme),
          frame_(frame),
          sx_(lv_area_get_width(&bounds) / 240.0f),
          sy_(lv_area_get_height(&bounds) / 144.0f) {}

    void Line(float x1, float y1, float x2, float y2, float width, unsigned color) {
        DrawLine(x1, y1, x2, y2, width, Color(color), LV_OPA_COVER);
    }

    void GlowLine(float x1, float y1, float x2, float y2, float width, unsigned color,
                  lv_opa_t opacity) {
        DrawLine(x1, y1, x2, y2, width, color, opacity);
    }

    void GlowBox(float x, float y, float width, float height, float radius, unsigned color,
                 lv_opa_t opacity) {
        DrawBox(x, y, width, height, radius, color, opacity);
    }

    void DrawLine(float x1, float y1, float x2, float y2, float width, unsigned color,
                  lv_opa_t opacity) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.p1.x = bounds_.x1 + static_cast<int>(x1 * sx_);
        dsc.p1.y = bounds_.y1 + static_cast<int>(y1 * sy_);
        dsc.p2.x = bounds_.x1 + static_cast<int>(x2 * sx_);
        dsc.p2.y = bounds_.y1 + static_cast<int>(y2 * sy_);
        dsc.width = std::max(1, static_cast<int>(width * std::min(sx_, sy_)));
        dsc.color = lv_color_hex(color);
        dsc.opa = opacity;
        dsc.round_start = dsc.round_end = theme_.style != "pixel";
        lv_draw_line(layer_, &dsc);
    }

    void Box(float x, float y, float width, float height, float radius, unsigned color) {
        const unsigned mapped_color = Color(color);
        if (color == kynangso::face_art::kCyan && theme_.style == "demon") {
            DrawBox(x - 5, y - 5, width + 10, height + 10, radius + 5, theme_.accent, 68);
            DrawBox(x + 2, y + 4, width, height, radius, Blend(theme_.background, 0x5A0714, 90),
                    LV_OPA_COVER);
            DrawBox(x, y, width, height, radius, mapped_color, LV_OPA_COVER);
            const float flicker = static_cast<float>((frame_ * 7) % 17);
            GlowLine(x + 4, y + height - 4 - flicker * 0.18f,
                     x + width - 4, y + height - 4, 1.5f, theme_.tear, 180);
            return;
        }
        if (color == kynangso::face_art::kCyan && theme_.style == "holo") {
            DrawBox(x - 4, y - 4, width + 8, height + 8, radius + 4, theme_.accent, 42);
            DrawBox(x + 2, y + 4, width, height, radius,
                    Blend(theme_.background, mapped_color, 78), LV_OPA_COVER);
            DrawBox(x, y, width, height, radius, mapped_color, LV_OPA_COVER);
            if (width > 20 && height > 16) {
                const float sweep_width = std::max(5.0f, width * 0.16f);
                const float travel = std::max(0.0f, width - sweep_width - 8.0f);
                const float progress = (frame_ % 96) / 95.0f;
                const float sweep_x = x + 4 + travel * progress;
                GlowLine(sweep_x, y + 4, sweep_x + sweep_width, y + height - 5, 2,
                         Blend(mapped_color, 0xFFFFFF, 175), 170);
            }
            return;
        }
        if (color == kynangso::face_art::kCyan && theme_.style == "plasma") {
            DrawBox(x - 5, y - 5, width + 10, height + 10, radius + 5, theme_.accent, 48);
            DrawBox(x + 2, y + 5, width, height, radius,
                    Blend(theme_.background, theme_.accent, 92), LV_OPA_COVER);
            DrawBox(x, y, width, height, radius, mapped_color, LV_OPA_COVER);
            if (width > 20 && height > 18) {
                DrawBox(x + 5, y + height * 0.72f, width - 10, std::max(2.0f, height * 0.10f),
                        radius * 0.35f, theme_.accent, 105);
            }
            return;
        }
        DrawBox(x, y, width, height, radius, mapped_color, LV_OPA_COVER);
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
        dsc.color = lv_color_hex(Color(color));
        dsc.opa = LV_OPA_COVER;
        lv_draw_triangle(layer_, &dsc);
    }

private:
    void DrawBox(float x, float y, float width, float height, float radius, unsigned color,
                 lv_opa_t opacity) {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(color);
        dsc.bg_opa = opacity;
        if (theme_.style == "pixel")
            radius = 0;
        else if (theme_.style == "soft" || theme_.style == "capybara" ||
                 theme_.style == "mochi")
            radius = std::min(width, height) / 2;
        dsc.radius = static_cast<int>(radius * std::min(sx_, sy_));
        const lv_area_t area = {
            bounds_.x1 + static_cast<int>(x * sx_),
            bounds_.y1 + static_cast<int>(y * sy_),
            bounds_.x1 + static_cast<int>((x + width) * sx_) - 1,
            bounds_.y1 + static_cast<int>((y + height) * sy_) - 1,
        };
        lv_draw_rect(layer_, &dsc, &area);
    }

    static unsigned Blend(unsigned from, unsigned to, unsigned amount) {
        amount = std::min(amount, 255u);
        unsigned result = 0;
        constexpr unsigned kChannelShifts[] = {0u, 8u, 16u};
        for (unsigned shift : kChannelShifts) {
            const unsigned a = (from >> shift) & 0xFF;
            const unsigned b = (to >> shift) & 0xFF;
            result |= ((a * (255 - amount) + b * amount) / 255) << shift;
        }
        return result;
    }

    unsigned Color(unsigned color) const {
        using namespace kynangso::face_art;
        if (color == kCyan)
            return theme_.primary;
        if (color == kPink)
            return theme_.accent;
        if (color == kTear)
            return theme_.tear;
        if (color == kBlack)
            return theme_.background;
        return color;
    }
    lv_layer_t* layer_;
    lv_area_t bounds_;
    const kynangso::FaceTheme& theme_;
    unsigned frame_;
    float sx_;
    float sy_;
};

void DrawThemeBackgroundEffects(FaceCanvas& canvas, const kynangso::FaceTheme& theme,
                                unsigned frame) {
    if (theme.style == "demon") {
        canvas.GlowLine(38, 48, 48, 21, 4, theme.accent, 120);
        canvas.Triangle(36, 51, 48, 16, 61, 49, theme.accent);
        canvas.GlowLine(202, 48, 192, 21, 4, theme.accent, 120);
        canvas.Triangle(204, 51, 192, 16, 179, 49, theme.accent);
        canvas.GlowLine(48, 20, 49, 40, 1.5f, theme.tear, 140);
        canvas.GlowLine(192, 20, 191, 40, 1.5f, theme.tear, 140);
    } else if (theme.style == "capybara") {
        canvas.GlowBox(49, 35, 18, 16, 9, theme.accent, 235);
        canvas.GlowBox(173, 35, 18, 16, 9, theme.accent, 235);
        canvas.GlowBox(54, 39, 8, 8, 4, theme.tear, 210);
        canvas.GlowBox(178, 39, 8, 8, 4, theme.tear, 210);
    } else if (theme.style == "shiba") {
        canvas.Triangle(34, 68, 42, 18, 78, 48, theme.accent);
        canvas.Triangle(206, 68, 198, 18, 162, 48, theme.accent);
        canvas.Triangle(43, 53, 46, 31, 64, 49, theme.tear);
        canvas.Triangle(197, 53, 194, 31, 176, 49, theme.tear);
    } else if (theme.style == "mochi") {
        canvas.GlowLine(32, 47, 40, 39, 2, theme.tear, 185);
        canvas.GlowLine(40, 39, 48, 47, 2, theme.tear, 185);
        canvas.GlowLine(40, 39, 40, 55, 2, theme.tear, 185);
        canvas.GlowLine(192, 112, 200, 104, 2, theme.accent, 185);
        canvas.GlowLine(200, 104, 208, 112, 2, theme.accent, 185);
        canvas.GlowLine(200, 104, 200, 120, 2, theme.accent, 185);
    } else if (theme.style == "holo") {
        const unsigned color = theme.accent;
        constexpr float kInset = 16;
        constexpr float kLength = 18;
        canvas.GlowLine(kInset, 20, kInset + kLength, 20, 1.5f, color, 90);
        canvas.GlowLine(kInset, 20, kInset, 36, 1.5f, color, 90);
        canvas.GlowLine(224, 20, 224 - kLength, 20, 1.5f, color, 90);
        canvas.GlowLine(224, 20, 224, 36, 1.5f, color, 90);
        canvas.GlowLine(kInset, 124, kInset + kLength, 124, 1.5f, color, 90);
        canvas.GlowLine(kInset, 124, kInset, 108, 1.5f, color, 90);
        canvas.GlowLine(224, 124, 224 - kLength, 124, 1.5f, color, 90);
        canvas.GlowLine(224, 124, 224, 108, 1.5f, color, 90);
        const float scan_y = 12 + static_cast<float>((frame * 2) % 120);
        canvas.GlowLine(24, scan_y, 216, scan_y, 1, theme.tear, 32);
    } else if (theme.style == "orbital") {
        constexpr unsigned kSegments = 10;
        const int gap = (frame / 8) % kSegments;
        for (int eye = 0; eye < 2; ++eye) {
            const float cx = eye == 0 ? 69.0f : 171.0f;
            for (unsigned i = 0; i < kSegments; ++i) {
                if (static_cast<int>(i) == gap || static_cast<int>(i) == (gap + 1) % kSegments)
                    continue;
                const float a0 = (i * 2.0f * kynangso::face_art::kPi) / kSegments;
                const float a1 = ((i + 1) * 2.0f * kynangso::face_art::kPi) / kSegments;
                canvas.GlowLine(cx + std::cos(a0) * 42, 72 + std::sin(a0) * 34,
                                cx + std::cos(a1) * 42, 72 + std::sin(a1) * 34, 1.5f,
                                theme.accent, 100);
            }
        }
    } else if (theme.style == "plasma") {
        for (unsigned i = 0; i < 4; ++i) {
            const float phase = frame * 0.045f + i * 1.5708f;
            const float x = 24 + i * 64;
            const float y = 24 + (std::sin(phase) + 1) * 44;
            canvas.GlowBox(x, y, 3, 3, 1.5f, theme.accent, 90);
        }
    }
}

void DrawThemeForegroundEffects(FaceCanvas& canvas, const kynangso::FaceTheme& theme,
                                unsigned frame) {
    if (theme.style == "capybara") {
        canvas.GlowBox(101, 99, 38, 22, 11, theme.accent, 245);
        canvas.GlowBox(112, 99, 16, 9, 4, theme.primary, 255);
        canvas.GlowBox(117, 102, 6, 4, 2, theme.background, 255);
        canvas.GlowLine(120, 106, 120, 113, 1, theme.background, 220);
        canvas.GlowLine(120, 113, 114, 116, 1, theme.background, 220);
        canvas.GlowLine(120, 113, 126, 116, 1, theme.background, 220);
    } else if (theme.style == "shiba") {
        canvas.GlowBox(42, 99, 20, 11, 6, theme.tear, 190);
        canvas.GlowBox(178, 99, 20, 11, 6, theme.tear, 190);
        canvas.GlowBox(108, 105, 24, 9, 5, theme.accent, 220);
    } else if (theme.style == "mochi") {
        const float pulse = 1.0f + (std::sin(frame * 0.12f) + 1.0f) * 0.8f;
        canvas.GlowBox(42, 99, 14 + pulse, 8 + pulse, 6, theme.accent, 205);
        canvas.GlowBox(184 - pulse, 99, 14 + pulse, 8 + pulse, 6, theme.accent, 205);
        canvas.GlowBox(114, 111, 12, 5, 2.5f, theme.tear, 245);
    } else if (theme.style == "orbital") {
        for (int eye = 0; eye < 2; ++eye) {
            const float phase = frame * 0.075f + eye * 2.5f;
            const float cx = eye == 0 ? 69.0f : 171.0f;
            const float x = cx + std::cos(phase) * 42;
            const float y = 72 + std::sin(phase) * 34;
            canvas.GlowBox(x - 3, y - 3, 6, 6, 3, theme.primary, 235);
        }
    } else if (theme.style == "plasma") {
        const float pulse = 8 + (std::sin(frame * 0.09f) + 1) * 2.5f;
        canvas.GlowBox(120 - pulse, 72 - pulse, pulse * 2, pulse * 2, pulse, theme.accent, 62);
        canvas.GlowBox(114, 66, 12, 12, 6, theme.primary, 225);
        canvas.GlowBox(117, 69, 6, 6, 3, 0xFFFFFF, 245);
    }
}
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
    lv_obj_set_style_bg_color(mouth_, lv_color_hex(kynangso::face_art::kCyan), 0);
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
#if CONFIG_LV_USE_GIF
    if (view->gif_loaded_)
        return;
#endif
    lv_area_t bounds;
    lv_obj_get_coords(view->root_, &bounds);
    // Leave room for the independent audio mouth, including wide surprise eyes.
    if (view->status_mode_ == RobotEyesMode::Speaking) {
        const int offset = lv_area_get_height(&bounds) / 24;
        bounds.y1 -= offset;
        bounds.y2 -= offset;
    }
    const unsigned frame =
        view->animation_enabled_ ? view->frame_ : (view->mode_ == RobotEyesMode::Boot ? 36 : 40);
    FaceCanvas canvas(lv_event_get_layer(event), bounds, view->theme_, frame);
    DrawThemeBackgroundEffects(canvas, view->theme_, frame);
    kynangso::face_art::Draw(canvas, view->mode_, frame,
                             view->status_mode_ == RobotEyesMode::Speaking);
    DrawThemeForegroundEffects(canvas, view->theme_, frame);
}

void RobotEyesView::OnTimer(lv_timer_t* timer) {
    auto* view = static_cast<RobotEyesView*>(lv_timer_get_user_data(timer));
    ++view->frame_;
    view->Render();
}

void RobotEyesView::SetTheme(const kynangso::FaceTheme& theme) {
#if CONFIG_LV_USE_GIF
    // Keep the current source alive until LVGL has detached its decoder. A
    // custom-expression change reuses one GIF widget and its fixed-size frame buffer.
    const auto previous_pair = theme_.gifs;
    const auto previous_gif = theme_.active_gif;
    const bool keep_gif_widget =
        gif_ != nullptr && ((previous_pair && theme.gifs) || (previous_gif && theme.active_gif));
    if (gif_ != nullptr && !keep_gif_widget) {
        lv_obj_delete(gif_);
        gif_ = nullptr;
        gif_loaded_ = false;
        gif_single_active_ = false;
    }
#endif
    theme_ = theme;
#if CONFIG_LV_USE_GIF
    auto setup = [](lv_image_dsc_t& descriptor, const kynangso::FaceGifData& data) {
        descriptor = {};
        descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor.header.cf = LV_COLOR_FORMAT_RAW;
        descriptor.data = data.bytes.get();
        descriptor.data_size = data.size;
    };
    if (theme_.active_gif) {
        const bool same_source = previous_gif == theme_.active_gif;
        setup(gif_single_, *theme_.active_gif);
        if (gif_ == nullptr)
            gif_ = lv_gif_create(root_);
        if (gif_ != nullptr) {
            lv_gif_set_auto_pause_invisible(gif_, true);
            if (!same_source || !gif_loaded_)
                lv_gif_set_src(gif_, &gif_single_);
            gif_single_active_ = true;
            gif_loaded_ = lv_gif_is_loaded(gif_);
            if (!gif_loaded_) {
                lv_obj_delete(gif_);
                gif_ = nullptr;
            } else {
                lv_obj_clear_flag(gif_, LV_OBJ_FLAG_HIDDEN);
                UpdateGif();
            }
        }
    } else if (theme_.gifs) {
        const bool same_source = previous_pair == theme_.gifs;
        setup(gif_idle_, theme_.gifs->idle);
        setup(gif_talking_, theme_.gifs->talking);
        if (gif_ == nullptr)
            gif_ = lv_gif_create(root_);
        if (gif_ != nullptr) {
            lv_gif_set_auto_pause_invisible(gif_, true);
            gif_single_active_ = false;
            if (!same_source || !gif_loaded_) {
                gif_talking_active_ = false;
                lv_gif_set_src(gif_, &gif_idle_);
            }
            gif_loaded_ = lv_gif_is_loaded(gif_);
            if (!gif_loaded_) {
                lv_obj_delete(gif_);
                gif_ = nullptr;
            } else {
                lv_obj_clear_flag(gif_, LV_OBJ_FLAG_HIDDEN);
                UpdateGif();
            }
        }
    } else if (gif_ != nullptr) {
        lv_obj_delete(gif_);
        gif_ = nullptr;
        gif_loaded_ = false;
        gif_single_active_ = false;
    }
#endif
    lv_obj_set_style_bg_color(root_, lv_color_hex(theme_.background), 0);
    lv_obj_set_style_bg_color(mouth_, lv_color_hex(theme_.primary), 0);
    lv_obj_set_style_bg_color(mouth_inside_, lv_color_hex(theme_.background), 0);
    SetAnimation(animation_enabled_);
    Render();
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
        if (enabled && visible_
#if CONFIG_LV_USE_GIF
            && !gif_loaded_
#endif
        )
            lv_timer_resume(timer_);
        else
            lv_timer_pause(timer_);
    }
#if CONFIG_LV_USE_GIF
    if (gif_loaded_) {
        if (enabled && visible_) lv_gif_resume(gif_);
        else lv_gif_pause(gif_);
    }
#endif
    Render();
}

void RobotEyesView::SetMode(RobotEyesMode mode) {
    // Repeated server packets must not restart the same expressive gesture.
    if (mode_ != mode) {
        mode_ = mode;
        frame_ = 0;
    }
#if CONFIG_LV_USE_GIF
    if (gif_ != nullptr && !gif_single_active_)
        UpdateGif();
#endif
    Render();
}

void RobotEyesView::SetSize(lv_coord_t width, lv_coord_t height) {
    width_ = width;
    height_ = height;
    lv_obj_set_size(root_, width_, height_);
    lv_obj_center(root_);
#if CONFIG_LV_USE_GIF
    if (gif_loaded_)
        UpdateGif();
#endif
    Render();
}

void RobotEyesView::SetStatus(RobotEyesMode mode) {
    if (mode != RobotEyesMode::Speaking || status_mode_ != RobotEyesMode::Speaking)
        SetSpeechLevel(0);
    status_mode_ = mode;
#if CONFIG_LV_USE_GIF
    if (gif_loaded_)
        UpdateGif();
#endif
    SetMode(mode);
}

void RobotEyesView::SetSpeechLevel(uint8_t level) {
    speech_target_ = level;
    speech_updated_ms_ = lv_tick_get();
    if (level == 0)
        speech_level_ = 0;
}

#if CONFIG_LV_USE_GIF
void RobotEyesView::UpdateGif() {
    if (gif_ == nullptr)
        return;
    if (gif_single_active_) {
        if (!gif_loaded_)
            return;
        if (!theme_.active_gif || theme_.active_gif->width == 0 || theme_.active_gif->height == 0)
            return;
        const auto& data = *theme_.active_gif;
        const int scale = std::min(256, std::min(static_cast<int>(width_) * 256 / data.width,
                                                 static_cast<int>(height_) * 256 / data.height));
        lv_image_set_scale(gif_, std::max(1, scale));
        lv_obj_center(gif_);
        if (!animation_enabled_ || !visible_)
            lv_gif_pause(gif_);
        return;
    }
    if (!theme_.gifs)
        return;

    // The legacy two-GIF theme only has neutral/talking artwork. Show the
    // procedural renderer for every other emotion instead of masking it.
    const bool use_legacy_gif = mode_ == RobotEyesMode::Neutral || mode_ == RobotEyesMode::Speaking;
    if (!use_legacy_gif) {
        if (gif_loaded_) {
            lv_obj_add_flag(gif_, LV_OBJ_FLAG_HIDDEN);
            gif_loaded_ = false;
            if (timer_ != nullptr && animation_enabled_ && visible_)
                lv_timer_resume(timer_);
        }
        return;
    }

    const bool talking = status_mode_ == RobotEyesMode::Speaking;
    if (!gif_loaded_ || talking != gif_talking_active_) {
        lv_gif_set_src(gif_, talking ? &gif_talking_ : &gif_idle_);
        gif_talking_active_ = talking;
        gif_loaded_ = lv_gif_is_loaded(gif_);
    }
    if (!gif_loaded_) {
        lv_obj_add_flag(gif_, LV_OBJ_FLAG_HIDDEN);
        if (timer_ != nullptr && animation_enabled_ && visible_)
            lv_timer_resume(timer_);
        return;
    }
    lv_obj_clear_flag(gif_, LV_OBJ_FLAG_HIDDEN);
    if (timer_ != nullptr)
        lv_timer_pause(timer_);
    const auto& data = talking ? theme_.gifs->talking : theme_.gifs->idle;
    const int scale = std::min(256, std::min(static_cast<int>(width_) * 256 / data.width,
                                             static_cast<int>(height_) * 256 / data.height));
    lv_image_set_scale(gif_, std::max(1, scale));
    lv_obj_center(gif_);
    if (!animation_enabled_ || !visible_)
        lv_gif_pause(gif_);
}
#endif

void RobotEyesView::RenderMouth() {
#if CONFIG_LV_USE_GIF
    if (gif_loaded_) {
        lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
#endif
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
    lv_obj_set_style_radius(mouth_, theme_.style == "pixel" ? 0 : mouth_height / 2, 0);
    if (mouth_height >= 10) {
        lv_obj_remove_flag(mouth_inside_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mouth_inside_, mouth_width - 8, mouth_height - 6);
        lv_obj_center(mouth_inside_);
        lv_obj_set_style_radius(mouth_inside_, theme_.style == "pixel" ? 0 : mouth_height / 2, 0);
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
        {"relaxed", RobotEyesMode::Neutral},
        {"delicious", RobotEyesMode::Delicious},
        {"yummy", RobotEyesMode::Delicious},
        {"laugh", RobotEyesMode::Laugh},
        {"funny", RobotEyesMode::Laugh},
        {"laughing", RobotEyesMode::Laugh},
        {"joy", RobotEyesMode::Laugh},
        {"surprised", RobotEyesMode::Surprised},
        {"shocked", RobotEyesMode::Surprised},
        {"astonished", RobotEyesMode::Surprised},
        {"wow", RobotEyesMode::Surprised},
        {"panic", RobotEyesMode::Surprised},
        {"sad", RobotEyesMode::Sad},
        {"cry", RobotEyesMode::Crying},
        {"crying", RobotEyesMode::Crying},
        {"disappointed", RobotEyesMode::Sad},
        {"embarrassed", RobotEyesMode::Embarrassed},
        {"shy", RobotEyesMode::Embarrassed},
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
        {"kiss", RobotEyesMode::Love},
        {"heart", RobotEyesMode::Love},
        {"devil", RobotEyesMode::Devil},
        {"mischievous", RobotEyesMode::Devil},
        {"silly", RobotEyesMode::Devil},
        {"confident", RobotEyesMode::Confident},
        {"cool", RobotEyesMode::Cool},
        {"playful", RobotEyesMode::Playful},
        {"wink", RobotEyesMode::Playful},
        {"winking", RobotEyesMode::Playful},
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
