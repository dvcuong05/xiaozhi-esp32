#ifndef KYNANGSO_ROBOT_MARKET_BULLETIN_VIEW_H_
#define KYNANGSO_ROBOT_MARKET_BULLETIN_VIEW_H_

#include <lvgl.h>

#include "features/market/market_bulletin.h"

namespace kynangso {

class RobotMarketBulletinView {
public:
    RobotMarketBulletinView(int width, int height, const lv_font_t* fallback_font);
    ~RobotMarketBulletinView();

    RobotMarketBulletinView(const RobotMarketBulletinView&) = delete;
    RobotMarketBulletinView& operator=(const RobotMarketBulletinView&) = delete;

    void SetTile(const MarketBulletinTile& tile, size_t index, size_t total);
    void SetVisible(bool visible);

private:
    int width_;
    int height_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* sequence_ = nullptr;
    lv_obj_t* source_ = nullptr;
    lv_obj_t* stale_badge_ = nullptr;
    lv_obj_t* rows_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    lv_obj_t* accent_ = nullptr;
    lv_timer_t* scroll_timer_ = nullptr;
    size_t scroll_index_ = 0;
    size_t first_scroll_index_ = 0;
    lv_font_t text_font_{};

    void Build();
    void StopScroll();
    void StartScroll();
    static void OnScrollTimer(lv_timer_t* timer);
};

}  // namespace kynangso

#endif  // KYNANGSO_ROBOT_MARKET_BULLETIN_VIEW_H_
