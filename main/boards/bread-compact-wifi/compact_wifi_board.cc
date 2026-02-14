#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "dht20_sensor.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <time.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    DHT20Sensor* dht20_sensor_;

    // Standby screen timer
    esp_timer_handle_t standby_timer_ = nullptr;
    float last_temperature_ = 0.0f;
    float last_humidity_ = 0.0f;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);

        // 初始化 DHT20 传感器（复用显示 I2C 总线）
        dht20_sensor_ = new DHT20Sensor(display_i2c_bus_, DHT20_I2C_ADDR);

        if (dht20_sensor_->IsInitialized()) {
            auto& mcp_server = McpServer::GetInstance();

            // 注册 MCP 工具：读取温湿度
            mcp_server.AddTool("sensor.read_temperature_humidity",
                "读取当前环境的温度和湿度数据",
                PropertyList(),
                [this](const PropertyList& properties) -> ReturnValue {
                    std::string data = dht20_sensor_->GetJsonData();
                    cJSON* result = cJSON_Parse(data.c_str());
                    return result;
                });

            ESP_LOGI("CompactWifiBoard", "DHT20 MCP tool registered");
        } else {
            ESP_LOGW("CompactWifiBoard", "DHT20 not initialized, skipping MCP tool registration");
        }

        // 初始化待机屏幕定时器
        const esp_timer_create_args_t standby_timer_args = {
            .callback = [](void* arg) {
                CompactWifiBoard* board = static_cast<CompactWifiBoard*>(arg);
                board->UpdateStandbyScreen();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "standby_timer"
        };
        esp_timer_create(&standby_timer_args, &standby_timer_);

        // 每 1 秒更新一次待机屏幕
        esp_timer_start_periodic(standby_timer_, 1000000); // 1 second

        // 监听设备状态变化
        Application::GetInstance().GetStateMachine().AddStateChangeListener(
            [this](DeviceState old_state, DeviceState new_state) {
                OnDeviceStateChanged(old_state, new_state);
            });
    }

    void OnDeviceStateChanged(DeviceState old_state, DeviceState new_state) {
        // 进入待机状态时显示待机屏幕
        if (new_state == kDeviceStateIdle) {
            OledDisplay* oled = dynamic_cast<OledDisplay*>(display_);
            if (oled) {
                oled->ShowStandbyScreen(true);
            }
        } else {
            // 其他状态隐藏待机屏幕
            OledDisplay* oled = dynamic_cast<OledDisplay*>(display_);
            if (oled) {
                oled->ShowStandbyScreen(false);
            }
        }
    }

    void UpdateStandbyScreen() {
        // 只在待机状态下更新
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            return;
        }

        // 获取当前时间
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        struct tm timeinfo;
        localtime_r(&tv.tv_sec, &timeinfo);

        char date_str[32];
        char time_str[32];

        // 格式化日期和星期
        const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
        snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d 星期%s",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 weekdays[timeinfo.tm_wday]);

        // 格式化时间
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // 读取温湿度
        float temp = last_temperature_;
        float humidity = last_humidity_;

        if (dht20_sensor_ != nullptr && dht20_sensor_->IsInitialized()) {
            float t, h;
            if (dht20_sensor_->ReadData(&t, &h)) {
                last_temperature_ = t;
                last_humidity_ = h;
                temp = t;
                humidity = h;
            }
        }

        // 更新显示
        OledDisplay* oled = dynamic_cast<OledDisplay*>(display_);
        if (oled) {
            oled->UpdateStandbyData(date_str, time_str, temp, humidity);
        }
    }
        static LampController lamp(LAMP_GPIO);

        // 初始化 DHT20 传感器（复用显示 I2C 总线）
        dht20_sensor_ = new DHT20Sensor(display_i2c_bus_, DHT20_I2C_ADDR);

        if (dht20_sensor_->IsInitialized()) {
            auto& mcp_server = McpServer::GetInstance();

            // 注册 MCP 工具：读取温湿度
            mcp_server.AddTool("sensor.read_temperature_humidity",
                "读取当前环境的温度和湿度数据",
                PropertyList(),
                [this](const PropertyList& properties) -> ReturnValue {
                    std::string data = dht20_sensor_->GetJsonData();
                    cJSON* result = cJSON_Parse(data.c_str());
                    return result;
                });

            ESP_LOGI("CompactWifiBoard", "DHT20 MCP tool registered");
        } else {
            ESP_LOGW("CompactWifiBoard", "DHT20 not initialized, skipping MCP tool registration");
        }
    }

    ~CompactWifiBoard() {
        if (standby_timer_) {
            esp_timer_stop(standby_timer_);
            esp_timer_delete(standby_timer_);
        }
        if (dht20_sensor_) {
            delete dht20_sensor_;
        }
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
        dht20_sensor_(nullptr) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);
