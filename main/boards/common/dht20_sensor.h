#ifndef DHT20_SENSOR_H
#define DHT20_SENSOR_H

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <cmath>
#include <string>

class DHT20Sensor {
private:
    i2c_master_dev_handle_t device_handle_;
    bool initialized_;
    float temperature_;
    float humidity_;
    float temperature_offset_;  // 温度校准偏移量
    float humidity_offset_;     // 湿度校准偏移量

public:
    DHT20Sensor(i2c_master_bus_handle_t i2c_bus, uint8_t i2c_addr = 0x38)
        : initialized_(false), temperature_(0.0f), humidity_(0.0f), temperature_offset_(0.0f), humidity_offset_(0.0f) {

        // 配置 I2C 设备（ESP-IDF 5.5+）
        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = i2c_addr,
            .scl_speed_hz = 100000,
        };

        esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_config, &device_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE("DHT20", "Failed to create I2C device: %s", esp_err_to_name(ret));
            return;
        }

        initialized_ = true;
        ESP_LOGI("DHT20", "DHT20 initialized at address 0x%02X", i2c_addr);
    }

    ~DHT20Sensor() {
        if (initialized_) {
            i2c_master_bus_rm_device(device_handle_);
        }
    }

    bool IsInitialized() const {
        return initialized_;
    }

    // 读取温度和湿度
    bool ReadData(float* temperature, float* humidity) {
        if (!initialized_) {
            return false;
        }

        // 发送测量命令
        uint8_t cmd[] = {0xAC, 0x33, 0x00};
        esp_err_t ret = i2c_master_transmit(device_handle_, cmd, sizeof(cmd), -1);
        if (ret != ESP_OK) {
            ESP_LOGE("DHT20", "Failed to send measure command: %s", esp_err_to_name(ret));
            return false;
        }

        // 等待测量完成（约80ms）- 使用 usleep 而不是 vTaskDelay
        // 因为 vTaskDelay 不能在定时器回调中使用
        usleep(80000); // 80ms

        // 读取数据
        uint8_t data[7] = {0};
        ret = i2c_master_receive(device_handle_, data, sizeof(data), -1);
        if (ret != ESP_OK) {
            ESP_LOGE("DHT20", "Failed to read data: %s", esp_err_to_name(ret));
            return false;
        }

        // 检查状态位
        if (data[0] & 0x80) {
            ESP_LOGE("DHT20", "Measurement not ready");
            return false;
        }

        // 解析数据
        uint32_t raw_humidity = ((data[1] << 12) | (data[2] << 4) | ((data[3] & 0xF0) >> 4));
        uint32_t raw_temperature = (((data[3] & 0x0F) << 16) | (data[4] << 8) | data[5]);

        // 转换为实际值
        *humidity = (raw_humidity * 100.0f) / 1048576.0f + humidity_offset_;
        *temperature = (raw_temperature * 200.0f) / 1048576.0f - 50.0f + temperature_offset_;

        // 更新缓存
        temperature_ = *temperature;
        humidity_ = *humidity;

        ESP_LOGI("DHT20", "Temperature: %.2f°C, Humidity: %.2f%%", *temperature, *humidity);
        return true;
    }

    // 只读取温度
    float GetTemperature() {
        float temp, hum;
        if (ReadData(&temp, &hum)) {
            return temp;
        }
        return temperature_;
    }

    // 只读取湿度
    float GetHumidity() {
        float temp, hum;
        if (ReadData(&temp, &hum)) {
            return hum;
        }
        return humidity_;
    }

    // 获取 JSON 格式的数据
    std::string GetJsonData() {
        float temp, hum;
        if (ReadData(&temp, &hum)) {
            char json[100];
            snprintf(json, sizeof(json),
                     "{\"temperature\": %.2f, \"humidity\": %.2f}",
                     temp, hum);
            return std::string(json);
        }
        return "{\"error\": \"Failed to read DHT20\"}";
    }

    // 设置温度校准偏移量（正数增加显示值，负数减少显示值）
    void SetTemperatureOffset(float offset) {
        temperature_offset_ = offset;
        ESP_LOGI("DHT20", "Temperature offset set to %.2f", offset);
    }

    // 设置湿度校准偏移量（正数增加显示值，负数减少显示值）
    void SetHumidityOffset(float offset) {
        humidity_offset_ = offset;
        ESP_LOGI("DHT20", "Humidity offset set to %.2f", offset);
    }

    // 获取温度校准偏移量
    float GetTemperatureOffset() const {
        return temperature_offset_;
    }

    // 获取湿度校准偏移量
    float GetHumidityOffset() const {
        return humidity_offset_;
    }

    // 校准温度（设置偏移量使显示值等于实际值）
    // actual_temp: 实际温度值（使用标准温度计测量）
    void CalibrateTemperature(float actual_temp) {
        float current_temp = GetTemperature();
        temperature_offset_ = actual_temp - current_temp;
        ESP_LOGI("DHT20", "Temperature calibrated: current=%.2f, actual=%.2f, offset=%.2f",
                 current_temp, actual_temp, temperature_offset_);
    }

    // 校准湿度（设置偏移量使显示值等于实际值）
    // actual_humidity: 实际湿度值（使用标准湿度计测量）
    void CalibrateHumidity(float actual_humidity) {
        float current_hum = GetHumidity();
        humidity_offset_ = actual_humidity - current_hum;
        ESP_LOGI("DHT20", "Humidity calibrated: current=%.2f, actual=%.2f, offset=%.2f",
                 current_hum, actual_humidity, humidity_offset_);
    }
};

#endif // DHT20_SENSOR_H
