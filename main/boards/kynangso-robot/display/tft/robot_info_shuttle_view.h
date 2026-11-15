#ifndef KYNANGSO_ROBOT_INFO_SHUTTLE_VIEW_H_
#define KYNANGSO_ROBOT_INFO_SHUTTLE_VIEW_H_

#include <lvgl.h>

#include <string>

namespace kynangso {

class RobotInfoShuttleView {
public:
    RobotInfoShuttleView(int width, int height, const lv_font_t* font, int calendar_theme = 0,
                         int weather_theme = 0);
    ~RobotInfoShuttleView();

    RobotInfoShuttleView(const RobotInfoShuttleView&) = delete;
    RobotInfoShuttleView& operator=(const RobotInfoShuttleView&) = delete;

    void SetFont(const lv_font_t* font);
    void SetColorThemes(int calendar_theme, int weather_theme);
    void ShowFace();
    void ShowSchedule(const std::string& summary);
    void ShowWeather(const std::string& summary, const std::string& configured_location,
                     int icon_kind = -1);
    void SetVisible(bool visible);

private:
    int width_;
    int height_;
    int calendar_theme_ = 0;
    int weather_theme_ = 0;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* face_page_ = nullptr;
    lv_obj_t* schedule_page_ = nullptr;
    lv_obj_t* weather_page_ = nullptr;
    lv_obj_t* schedule_heading_ = nullptr;
    lv_obj_t* schedule_when_ = nullptr;
    lv_obj_t* schedule_event_ = nullptr;
    lv_obj_t* schedule_footer_ = nullptr;
    lv_obj_t* weather_temperature_ = nullptr;
    lv_obj_t* weather_location_ = nullptr;
    lv_obj_t* weather_condition_ = nullptr;
    lv_obj_t* weather_footer_ = nullptr;
    lv_obj_t* weather_icon_sun_ = nullptr;
    lv_obj_t* weather_icon_cloud_ = nullptr;
    lv_obj_t* weather_icon_storm_ = nullptr;
    lv_obj_t* weather_icon_rain_group_ = nullptr;
    lv_obj_t* weather_icon_wind_group_ = nullptr;
    lv_obj_t* weather_icon_snow_ = nullptr;
    lv_obj_t* weather_icon_fog_ = nullptr;
    lv_obj_t* weather_icon_rain_[3] = {};
    lv_obj_t* weather_icon_storm_rain_[3] = {};
    lv_obj_t* weather_icon_flash_ = nullptr;
    lv_font_t text_font_{};
    lv_font_t title_font_{};
    const lv_font_t* large_font_ = nullptr;
    void SetWeatherIcon(int icon_kind);
    void StartWeatherAnimation();
    void StopWeatherAnimation();
    int weather_icon_kind_ = -1;
    bool weather_animation_active_ = false;

    void BuildSchedulePage();
    void BuildWeatherPage();
    void SelectPage(lv_obj_t* page);
};

}  // namespace kynangso

#endif  // KYNANGSO_ROBOT_INFO_SHUTTLE_VIEW_H_
