#include "standby_screen.h"
#include "lvgl_theme.h"
#include "board.h"
#include <esp_log.h>
#include <esp_sntp.h>
#include <time.h>
#include <sys/time.h>
#include <cstring>
#include <cmath>

#define TAG "StandbyScreen"

// 字体声明
LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

StandbyScreen::StandbyScreen(int width, int height)
    : width_(width)
    , height_(height)
    , is_visible_(false)
    , container_(nullptr)
    , date_label_(nullptr)
    , weekday_label_(nullptr)
    , time_label_(nullptr)
    , temperature_label_(nullptr)
    , humidity_label_(nullptr)
    , temp_icon_(nullptr)
    , humidity_icon_(nullptr)
    , divider_line_(nullptr)
    , update_timer_(nullptr)
    , current_temperature_(NAN)
    , current_humidity_(NAN) {

    // 设置时区 (UTC+8)
    setenv("TZ", "CST-8", 1);
    tzset();

    // 创建定时器
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            StandbyScreen* screen = static_cast<StandbyScreen*>(arg);
            screen->UpdateTimerCallback();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "standby_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&timer_args, &update_timer_);
}

StandbyScreen::~StandbyScreen() {
    Hide();
    if (update_timer_ != nullptr) {
        esp_timer_stop(update_timer_);
        esp_timer_delete(update_timer_);
    }
}

void StandbyScreen::CreateUI() {
    if (container_ != nullptr) {
        return;
    }

    auto& theme_manager = LvglThemeManager::GetInstance();
    auto* theme = theme_manager.GetTheme("light");
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();

    auto screen = lv_screen_active();

    // 主容器 - 使用flex布局垂直排列三行
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_center(container_);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 第一行：日期和星期
    lv_obj_t* row1 = lv_obj_create(container_);
    lv_obj_set_size(row1, width_, height_ / 3);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 8, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    date_label_ = lv_label_create(row1);
    lv_label_set_text(date_label_, "");
    lv_obj_set_style_text_font(date_label_, text_font, 0);
    lv_obj_set_style_text_color(date_label_, lv_color_white(), 0);

    weekday_label_ = lv_label_create(row1);
    lv_label_set_text(weekday_label_, "");
    lv_obj_set_style_text_font(weekday_label_, text_font, 0);
    lv_obj_set_style_text_color(weekday_label_, lv_color_white(), 0);
    lv_obj_set_style_margin_left(weekday_label_, 16, 0);

    // 第二行：时钟
    lv_obj_t* row2 = lv_obj_create(container_);
    lv_obj_set_size(row2, width_, height_ / 3);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    time_label_ = lv_label_create(row2);
    lv_label_set_text(time_label_, "--:--");
    lv_obj_set_style_text_font(time_label_, text_font, 0);
    lv_obj_set_style_text_color(time_label_, lv_color_white(), 0);

    // 第三行：温湿度（左右分布）
    lv_obj_t* row3 = lv_obj_create(container_);
    lv_obj_set_size(row3, width_, height_ / 3);
    lv_obj_set_style_bg_opa(row3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row3, 0, 0);
    lv_obj_set_style_pad_all(row3, 8, 0);
    lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row3, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 左边：温度
    lv_obj_t* temp_container = lv_obj_create(row3);
    lv_obj_set_size(temp_container, width_ / 2 - 8, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(temp_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(temp_container, 0, 0);
    lv_obj_set_style_pad_all(temp_container, 0, 0);
    lv_obj_set_flex_flow(temp_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    temp_icon_ = lv_label_create(temp_container);
    lv_label_set_text(temp_icon_, LV_SYMBOL_IMAGE "°C");
    lv_obj_set_style_text_font(temp_icon_, icon_font, 0);
    lv_obj_set_style_text_color(temp_icon_, lv_color_hex(0xFF5722), 0);

    temperature_label_ = lv_label_create(temp_container);
    lv_label_set_text(temperature_label_, "--.-°C");
    lv_obj_set_style_text_font(temperature_label_, text_font, 0);
    lv_obj_set_style_text_color(temperature_label_, lv_color_white(), 0);
    lv_obj_set_style_margin_left(temperature_label_, 8, 0);

    // 右边：湿度
    lv_obj_t* humidity_container = lv_obj_create(row3);
    lv_obj_set_size(humidity_container, width_ / 2 - 8, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(humidity_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(humidity_container, 0, 0);
    lv_obj_set_style_pad_all(humidity_container, 0, 0);
    lv_obj_set_flex_flow(humidity_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(humidity_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    humidity_icon_ = lv_label_create(humidity_container);
    lv_label_set_text(humidity_icon_, LV_SYMBOL_SETTINGS "%");
    lv_obj_set_style_text_font(humidity_icon_, icon_font, 0);
    lv_obj_set_style_text_color(humidity_icon_, lv_color_hex(0x2196F3), 0);

    humidity_label_ = lv_label_create(humidity_container);
    lv_label_set_text(humidity_label_, "--.-%");
    lv_obj_set_style_text_font(humidity_label_, text_font, 0);
    lv_obj_set_style_text_color(humidity_label_, lv_color_white(), 0);
    lv_obj_set_style_margin_left(humidity_label_, 8, 0);

    // 分割线
    divider_line_ = lv_obj_create(container_);
    lv_obj_set_size(divider_line_, width_ - 32, 1);
    lv_obj_align(divider_line_, LV_ALIGN_TOP_MID, 0, 2 * height_ / 3);
    lv_obj_set_style_bg_color(divider_line_, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(divider_line_, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(divider_line_, 0, 0);
}

void StandbyScreen::DestroyUI() {
    if (container_ != nullptr) {
        lv_obj_del(container_);
        container_ = nullptr;
        date_label_ = nullptr;
        weekday_label_ = nullptr;
        time_label_ = nullptr;
        temperature_label_ = nullptr;
        humidity_label_ = nullptr;
        temp_icon_ = nullptr;
        humidity_icon_ = nullptr;
        divider_line_ = nullptr;
    }
}

void StandbyScreen::Show() {
    if (is_visible_) {
        ESP_LOGI("StandbyScreen", "Already visible, skipping Show()");
        return;
    }

    ESP_LOGI("StandbyScreen", "Show() called, creating UI...");
    CreateUI();
    is_visible_ = true;

    // 启动定时更新（每秒更新时间）
    esp_err_t ret = esp_timer_start_periodic(update_timer_, 1000000); // 1秒
    if (ret != ESP_OK) {
        ESP_LOGE("StandbyScreen", "Failed to start timer: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI("StandbyScreen", "Timer started successfully");
    }
}

void StandbyScreen::Hide() {
    if (!is_visible_) {
        return;
    }

    esp_timer_stop(update_timer_);

    DestroyUI();
    is_visible_ = false;
}

void StandbyScreen::UpdateTime(const char* date, const char* weekday, const char* time) {
    cached_date_ = date;
    cached_weekday_ = weekday;
    cached_time_ = time;
    lv_async_call([](void* ctx) {
        StandbyScreen* screen = static_cast<StandbyScreen*>(ctx);
        screen->UpdateTimeUI();
    }, this);
}

void StandbyScreen::UpdateTimeUI() {
    if (!is_visible_ || date_label_ == nullptr) {
        ESP_LOGW("StandbyScreen", "UpdateTimeUI skipped - is_visible=%d, date_label=%p", is_visible_, (void*)date_label_);
        return;
    }

    ESP_LOGI("StandbyScreen", "Updating UI with time: date=%s, weekday=%s, time=%s",
             cached_date_.c_str(), cached_weekday_.c_str(), cached_time_.c_str());
    lv_label_set_text(date_label_, cached_date_.c_str());
    lv_label_set_text(weekday_label_, cached_weekday_.c_str());
    lv_label_set_text(time_label_, cached_time_.c_str());
}

void StandbyScreen::UpdateTemperatureHumidity(float temperature, float humidity) {
    current_temperature_ = temperature;
    current_humidity_ = humidity;

    if (!is_visible_ || temperature_label_ == nullptr) {
        return;
    }

    lv_async_call([](void* ctx) {
        StandbyScreen* screen = static_cast<StandbyScreen*>(ctx);
        screen->UpdateTemperatureHumidityUI();
    }, this);
}

void StandbyScreen::UpdateTemperatureHumidityUI() {
    if (!is_visible_ || temperature_label_ == nullptr) {
        ESP_LOGW("StandbyScreen", "UpdateTemperatureHumidityUI skipped - is_visible=%d, temperature_label=%p",
                 is_visible_, (void*)temperature_label_);
        return;
    }

    char temp_buf[32];
    char humi_buf[32];

    // 检查是否为NaN（未连接传感器）
    if (std::isnan(current_temperature_)) {
        snprintf(temp_buf, sizeof(temp_buf), "--.-°C");
        ESP_LOGW("StandbyScreen", "Temperature is NaN, displaying placeholder");
    } else {
        snprintf(temp_buf, sizeof(temp_buf), "%.1f°C", current_temperature_);
    }

    if (std::isnan(current_humidity_)) {
        snprintf(humi_buf, sizeof(humi_buf), "--.-%%");
        ESP_LOGW("StandbyScreen", "Humidity is NaN, displaying placeholder");
    } else {
        snprintf(humi_buf, sizeof(humi_buf), "%.1f%%", current_humidity_);
    }

    ESP_LOGI("StandbyScreen", "Updating temperature/humidity UI: %s %s", temp_buf, humi_buf);
    lv_label_set_text(temperature_label_, temp_buf);
    lv_label_set_text(humidity_label_, humi_buf);
}

void StandbyScreen::StartUpdate() {
    if (update_timer_ != nullptr) {
        esp_timer_start_periodic(update_timer_, 1000000); // 1秒
    }
}

void StandbyScreen::StopUpdate() {
    if (update_timer_ != nullptr) {
        esp_timer_stop(update_timer_);
    }
}

void StandbyScreen::UpdateTimerCallback() {
    ESP_LOGI("StandbyScreen", "Timer callback triggered");
    // 获取当前时间
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // 格式化日期
    char date_buf[32];
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

    // 星期数组
    const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const char* weekday = weekdays[timeinfo.tm_wday];

    // 格式化时间
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    ESP_LOGI("StandbyScreen", "Updating time: %s %s %s", date_buf, weekday, time_buf);
    UpdateTime(date_buf, weekday, time_buf);
}
