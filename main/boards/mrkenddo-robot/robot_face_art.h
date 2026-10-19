#ifndef MRKENDDO_ROBOT_FACE_ART_H_
#define MRKENDDO_ROBOT_FACE_ART_H_

#include <cmath>

enum class RobotEyesMode : unsigned char {
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
    Laugh,
    Love,
    Dizzy,
    Distracted,
    Devil,
    Confident,
    Yawn,
    Playful,
};

namespace mrkenddo::face_art {

constexpr unsigned kCyan = 0x0ABDE3;
constexpr unsigned kPink = 0xFF70AE;
constexpr unsigned kTear = 0x69D9FF;
constexpr unsigned kBlack = 0x000000;
constexpr float kPi = 3.14159265f;

// Fixed 240 x 144 design space. Both LVGL and the desktop preview consume
// these same primitives; rendering needs no frame buffer or per-frame heap.
template <typename Canvas>
void Curve(Canvas& c, float x, float y, float width, float depth, unsigned color = kCyan,
           float stroke = 5) {
    float px = x, py = y;
    for (int i = 1; i <= 10; ++i) {
        const float u = i / 10.0f;
        const float nx = x + width * u;
        const float ny = y + depth * 4 * u * (1 - u);
        c.Line(px, py, nx, ny, stroke, color);
        px = nx;
        py = ny;
    }
}

template <typename Canvas>
void Heart(Canvas& c, float x, float y, float size) {
    // Three primitives per heart, instead of dozens of scanline draw tasks.
    c.Box(x - size * 0.46f, y, size * 0.50f, size * 0.50f, size * 0.25f, kPink);
    c.Box(x - size * 0.04f, y, size * 0.50f, size * 0.50f, size * 0.25f, kPink);
    c.Triangle(x - size * 0.43f, y + size * 0.34f, x + size * 0.43f, y + size * 0.34f, x, y + size,
               kPink);
}

template <typename Canvas>
void Spiral(Canvas& c, float x, float y, float phase) {
    float px = x, py = y;
    for (int i = 1; i <= 32; ++i) {
        const float a = i * 0.40f + phase;
        const float r = i * 0.65f;
        const float nx = x + std::cos(a) * r;
        const float ny = y + std::sin(a) * r;
        c.Line(px, py, nx, ny, 4, kCyan);
        px = nx;
        py = ny;
    }
}

template <typename Canvas>
void Draw(Canvas& c, RobotEyesMode mode, unsigned frame, bool speaking) {
    const float t = frame * 0.04f;
    const float wave = std::sin(t * 3);
    float left_y = 48, right_y = 48, eye_h = 48, eye_w = 58;
    float gaze = 0;
    bool panels = true;
    // Blink late in the cycle: an emotion must be readable on its first frame.
    const unsigned blink = (frame + 72) % 153;
    bool can_blink = true;
    switch (mode) {
        case RobotEyesMode::Boot:
            eye_h = 4 + 44 * (frame < 36 ? frame / 36.0f : 1);
            left_y = right_y = 72 - eye_h / 2;
            can_blink = false;
            break;
        case RobotEyesMode::Neutral:
        case RobotEyesMode::Speaking:
            // Look, settle, then look the other way; brief pauses add intention.
            gaze = std::sin(t * 0.65f) * 7;
            left_y += std::sin(t * 1.3f) * 1.5f;
            right_y = left_y;
            break;
        case RobotEyesMode::Listening:
            eye_h = 54 + wave * 2;
            left_y = 43;
            right_y = 47;
            c.Box(41, 28, 42, 5, 2, kCyan);
            c.Box(151, 31, 34, 5, 2, kCyan);
            break;
        case RobotEyesMode::Happy:
        case RobotEyesMode::Laugh:
            panels = false;
            for (int side = 0; side < 2; ++side) {
                const float x = 69.0f + side * 102;
                const float y = 65 + (mode == RobotEyesMode::Laugh ? wave * 3 : wave);
                if (mode == RobotEyesMode::Laugh) {
                    const float direction = side == 0 ? 1 : -1;
                    c.Line(x - 23 * direction, y - 12, x + 14 * direction, y, 7, kCyan);
                    c.Line(x + 14 * direction, y, x - 23 * direction, y + 10, 7, kCyan);
                } else {
                    Curve(c, x - 25, y + 8, 50, -19, kCyan, 7);
                }
                c.Line(x - 17, 91, x - 11, 87, 3, kPink);
                c.Line(x - 6, 92, x, 88, 3, kPink);
            }
            break;
        case RobotEyesMode::Sad:
            eye_h = 26;
            left_y = right_y = 63;
            c.Line(44, 49, 85, 36, 5, kCyan);
            c.Line(155, 36, 196, 49, 5, kCyan);
            // One tear travels slowly down the cheek, then forms again.
            c.Box(87, 87 + (frame % 70) * 0.35f, 6, 11, 3, kTear);
            break;
        case RobotEyesMode::Angry:
        case RobotEyesMode::Devil:
            panels = false;
            // Slanted, inward-pointing wedges rather than resized neutral eyes.
            c.Triangle(43, 50, 94, 68, 94, 83, kCyan);
            c.Triangle(43, 50, 94, 83, 43, 72, kCyan);
            c.Triangle(146, 68, 197, 50, 146, 83, kCyan);
            c.Triangle(197, 50, 197, 72, 146, 83, kCyan);
            c.Line(42, 36, 94, 55, 6, kCyan);
            c.Line(146, 55, 198, 36, 6, kCyan);
            if (mode == RobotEyesMode::Devil) {
                c.Line(44, 28, 37, 12, 7, kPink);
                c.Line(196, 28, 203, 12, 7, kPink);
            }
            break;
        case RobotEyesMode::Surprised:
            eye_w = 64;
            eye_h = 62 + wave * 3;
            left_y = 34;
            right_y = 38;
            c.Line(45, 24, 83, 20, 4, kCyan);
            c.Line(157, 20, 195, 24, 4, kCyan);
            break;
        case RobotEyesMode::Sleepy:
        case RobotEyesMode::Yawn:
            if (mode == RobotEyesMode::Yawn) {
                const unsigned phase = frame % 125;
                eye_h = phase < 25 ? 30 - phase : (phase > 100 ? phase - 94 : 6);
            } else {
                eye_h = 12 + wave * 3;
            }
            left_y = 69 + wave * 3;
            right_y = left_y + 3;
            can_blink = false;
            if (mode == RobotEyesMode::Sleepy) {
                const float y = 28 - (frame % 60) * 0.25f;
                c.Line(204, y, 216, y, 2, kCyan);
                c.Line(216, y, 204, y + 12, 2, kCyan);
                c.Line(204, y + 12, 216, y + 12, 2, kCyan);
            }
            break;
        case RobotEyesMode::Distracted:
            left_y = 55 + wave * 3;
            right_y = 58 - wave * 3;
            eye_h = 21;
            eye_w = 52;
            gaze = std::sin(t * 2) * 14;
            for (int dot = 0; dot < 3; ++dot)
                c.Box(104 + dot * 13, 27 - std::sin(t * 2 + dot) * 3, 5, 5, 2, kCyan);
            break;
        case RobotEyesMode::Confused:
            left_y = 43 + wave * 4;
            right_y = 63 - wave * 3;
            eye_w = 50;
            eye_h = 37;
            gaze = -5;
            c.Line(43, 30 + wave * 2, 85, 23, 5, kCyan);
            c.Line(155, 44, 190, 48, 4, kCyan);
            break;
        case RobotEyesMode::Love:
            panels = false;
            Heart(c, 69, 42 - wave * 2, 50 + wave * 2);
            Heart(c, 171, 42 + wave * 2, 50 - wave * 2);
            Heart(c, 205, 16 + (1 - std::sin(t * 2)) * 5, 12);
            break;
        case RobotEyesMode::Dizzy:
            panels = false;
            Spiral(c, 69, 67 + wave * 3, t * 3);
            Spiral(c, 171, 67 - wave * 3, -t * 3);
            break;
        case RobotEyesMode::Confident:
            panels = false;
            c.Box(38, 47, 65, 35, 9, kCyan);
            c.Box(137, 47, 65, 35, 9, kCyan);
            c.Line(103, 55, 137, 55, 6, kCyan);
            c.Line(44, 54, 58, 54, 3, kBlack);
            c.Line(143, 54, 157, 54, 3, kBlack);
            break;
        case RobotEyesMode::Playful:
            panels = false;
            c.Box(40, 43, 58, 49, 15, kCyan);
            c.Line(152, 53, 190, 65, 7, kCyan);
            c.Line(190, 65, 152, 77, 7, kCyan);
            break;
    }
    if (panels) {
        if (can_blink && blink < 5) {
            left_y += eye_h / 2;
            right_y += eye_h / 2;
            eye_h = 4;
        }
        c.Box(69 - eye_w / 2 + gaze, left_y, eye_w, eye_h, 14, kCyan);
        c.Box(171 - eye_w / 2 + gaze, right_y, eye_w,
              mode == RobotEyesMode::Confused ? eye_h * 0.65f : eye_h, 14, kCyan);
    }
    if (speaking)
        return;  // The audio envelope continues to own the speaking mouth.
    switch (mode) {
        case RobotEyesMode::Laugh:
            c.Box(101, 100, 38, 22 + wave * 3, 10, kCyan);
            c.Box(108, 101, 24, 5, 2, kBlack);
            break;
        case RobotEyesMode::Yawn: {
            const unsigned phase = frame % 125;
            const float stretch =
                phase > 25 && phase < 100 ? std::sin((phase - 25) / 75.0f * kPi) : 0;
            const float opening = 10 + stretch * 34;
            c.Box(105, 93, 30, opening, 15, kCyan);
            if (opening > 16)
                c.Box(112, 99, 16, opening - 12, 8, kBlack);
            break;
        }
        case RobotEyesMode::Surprised:
            c.Box(112, 106, 16, 22, 8, kCyan);
            c.Box(117, 111, 6, 12, 3, kBlack);
            break;
        case RobotEyesMode::Sad:
            Curve(c, 102, 119, 36, -8);
            break;
        case RobotEyesMode::Angry:
            c.Line(104, 113, 137, 113, 4, kCyan);
            break;
        case RobotEyesMode::Confused:
        case RobotEyesMode::Distracted:
        case RobotEyesMode::Dizzy:
            c.Line(104, 115, 114, 111, 3, kCyan);
            c.Line(114, 111, 124, 117, 3, kCyan);
            c.Line(124, 117, 136, 112, 3, kCyan);
            break;
        case RobotEyesMode::Sleepy:
            c.Box(112, 112, 16, 5, 2, kCyan);
            break;
        default:
            Curve(c, 100, 107, 40, mode == RobotEyesMode::Devil ? 16 : 9);
            break;
    }
}

}  // namespace mrkenddo::face_art
#endif
