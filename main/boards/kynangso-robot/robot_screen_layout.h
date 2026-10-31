#ifndef KYNANGSO_ROBOT_SCREEN_LAYOUT_H_
#define KYNANGSO_ROBOT_SCREEN_LAYOUT_H_

#include <lvgl.h>

#include <string>

namespace kynangso {

// Normal presentation only. Face renderers attach to FaceRegion(), while QR,
// boot console and spectrum remain separate full-screen presentations.
// All operations require the owning display's LVGL lock.
class RobotScreenLayout {
public:
    RobotScreenLayout(lv_obj_t* parent, int width, int height, const lv_font_t* font);
    ~RobotScreenLayout();
    RobotScreenLayout(const RobotScreenLayout&) = delete;
    RobotScreenLayout& operator=(const RobotScreenLayout&) = delete;

    lv_obj_t* FaceRegion() const { return face_region_; }
    int FaceHeight() const { return face_height_; }
    void SetVisible(bool visible);
    void SetText(const char* role, const char* text);
    void RemoveText(const char* role, const char* text);
    void ClearText();
    void ShowStartupHint(const char* text);
    void HideStartupHint(const char* text);
    void DismissStartupHint();
    void SetHideSubtitle(bool hide);
    void SetFont(const lv_font_t* font);

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* face_region_ = nullptr;
    lv_obj_t* info_region_ = nullptr;
    lv_obj_t* info_label_ = nullptr;
    int width_;
    int height_;
    int face_height_ = 0;
    bool startup_hint_ = false;
    bool hide_subtitle_ = false;
    std::string role_;
    std::string text_;

    void RenderText();
};

}  // namespace kynangso

#endif  // KYNANGSO_ROBOT_SCREEN_LAYOUT_H_
