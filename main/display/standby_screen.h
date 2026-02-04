#ifndef STANDBY_SCREEN_H
#define STANDBY_SCREEN_H

#include <lvgl.h>
#include <esp_timer.h>
#include <string>
#include <memory>

class StandbyScreen {
public:
    StandbyScreen(int width, int height);
    ~StandbyScreen();

    // 显示待机界面
    void Show();

    // 隐藏待机界面
    void Hide();

    // 更新时间显示
    void UpdateTime(const char* date, const char* weekday, const char* time);

    // 更新温湿度显示
    void UpdateTemperatureHumidity(float temperature, float humidity);

    // 启动定时更新
    void StartUpdate();

    // 停止定时更新
    void StopUpdate();

    // 检查是否可见
    bool IsVisible() const { return is_visible_; }

private:
    void CreateUI();
    void DestroyUI();
    void UpdateTimerCallback();
    void UpdateTimeUI();
    void UpdateTemperatureHumidityUI();

    int width_;
    int height_;
    bool is_visible_;

    // UI 元素
    lv_obj_t* container_;
    lv_obj_t* date_label_;
    lv_obj_t* weekday_label_;
    lv_obj_t* time_label_;
    lv_obj_t* temperature_label_;
    lv_obj_t* humidity_label_;
    lv_obj_t* temp_icon_;
    lv_obj_t* humidity_icon_;
    lv_obj_t* divider_line_;

    // 定时器
    esp_timer_handle_t update_timer_;
    float current_temperature_;
    float current_humidity_;

    // 缓存的时间字符串
    std::string cached_date_;
    std::string cached_weekday_;
    std::string cached_time_;
};

#endif // STANDBY_SCREEN_H
