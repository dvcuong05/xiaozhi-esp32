#ifndef MRKENDDO_ROBOT_OLED_DISPLAY_CONTROLLER_H_
#define MRKENDDO_ROBOT_OLED_DISPLAY_CONTROLLER_H_

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace mrkenddo {

enum class OledDisplayMode : int32_t {
    Clock = 0,
    Marquee = 1,
    Status = 2,
    Off = 3,
};

struct OledDisplaySettings {
    OledDisplayMode mode = OledDisplayMode::Clock;
    char text[241] = {};
    int32_t speed = 2;
    int32_t font_size = 0;
};

class OledDisplayController {
public:
    static constexpr size_t kMaxTextLength = sizeof(OledDisplaySettings::text) - 1;

    void Configure(uint8_t* framebuffer, size_t framebuffer_size, int width, int height,
                   int rotation);
    void Disable();

    bool Render(const OledDisplaySettings& settings, const char* status_line_1,
                const char* status_line_2, time_t now);

    static bool IsValidMode(int32_t mode);
    static bool IsValidSpeed(int32_t speed);
    static bool IsValidFontSize(int32_t font_size);
    static void NormalizeText(const char* input, char* output, size_t output_size);

private:
    uint8_t* framebuffer_ = nullptr;
    size_t framebuffer_size_ = 0;
    int width_ = 0;
    int height_ = 0;
    int rotation_ = 0;
    int marquee_offset_ = 0;
    OledDisplayMode last_mode_ = OledDisplayMode::Clock;
    char last_text_[sizeof(OledDisplaySettings::text)] = {};

    void Clear();
    void SetPixel(int x, int y);
    void DrawText(const char* text, int x, int y, int scale);
    void DrawCenteredText(const char* text, int y, int scale);
    int TextWidth(const char* text, int scale) const;
    int ChooseScale(const char* text, int max_scale) const;
    void DrawClock(time_t now);
    void DrawStatus(const char* line_1, const char* line_2);
    void DrawMarquee(const OledDisplaySettings& settings);
};

}  // namespace mrkenddo

#endif  // MRKENDDO_ROBOT_OLED_DISPLAY_CONTROLLER_H_
