# DHT20 温湿度传感器 + 待机界面功能说明

## 功能概述

为 ESP32-S3 添加 DHT20 温湿度传感器，并在 LCD 屏幕上实现待机界面，显示：
- 第一行：日期和星期
- 第二行：时钟
- 第三行：温度（左）和湿度（右）

唤醒后自动切换到与小智对话界面。

---

## 硬件连接

### DHT20 温湿度传感器（I2C）

| DHT20 引脚 | ESP32-S3 引脚 | 说明 |
|-----------|--------------|------|
| VCC | 3.3V | 注意：必须接 3.3V |
| GND | GND | 地线 |
| SDA | GPIO 41 | I2C 数据线（与 OLED 复用） |
| SCL | GPIO 42 | I2C 时钟线（与 OLED 复用） |

**注意**：DHT20 和 LCD 屏幕共享同一组 I2C 总线（GPIO 41/42）。

### LCD 屏幕（240x240）

| LCD 引脚 | ESP32-S3 引脚 | 说明 |
|---------|--------------|------|
| VCC | 3.3V | 电源 |
| GND | GND | 地线 |
| SCL | GPIO 21 | SPI 时钟 |
| SDA | GPIO 47 | SPI 数据 |
| RES | GPIO 45 | 复位 |
| DC | GPIO 40 | 数据/命令 |
| CS | GPIO 41 | 片选 |
| BL | GPIO 42 | 背光 |

---

## 编译配置

### 1. 在 menuconfig 中选择开发板

```
idf.py menuconfig
```

### 2. 配置选项

```
Xiaozhi Assistant → Board Type
    → Bread Compact WiFi + LCD (面包板)
```

```
Xiaozhi Assistant → LCD Type
    → ST7789 240x240 (或你的屏幕型号)
```

### 3. 设置 Flash 大小（如果需要）

根据你的板子修改 `sdkconfig.defaults.esp32s3`：

```ini
# 4MB
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y

# 或 8MB
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y

# 或 16MB（默认）
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
```

---

## 编译和烧录

```powershell
# 1. 设置目标芯片
idf.py set-target esp32s3

# 2. 配置
idf.py menuconfig
# → Board Type: Bread Compact WiFi + LCD
# → LCD Type: ST7789 240x240

# 3. 编译
idf.py build

# 4. 烧录
idf.py -p COM3 flash

# 5. 监视日志
idf.py -p COM3 monitor
```

---

## 功能说明

### 待机界面（空闲状态）

当设备处于 **空闲状态**（`kDeviceStateIdle`）时：

1. **第一行**：显示当前日期和星期
   - 格式：`2025-02-04 周二`

2. **第二行**：显示当前时间
   - 格式：`14:30:25`（秒数实时更新）

3. **第三行**：显示温湿度
   - 左侧：`🌡️ 25.5°C`（橙色）
   - 右侧：`💧 60.3%`（蓝色）

### 对话界面（唤醒后）

当检测到以下状态时，自动切换到对话界面：
- `kDeviceStateListening` - 正在监听
- `kDeviceStateSpeaking` - 正在说话
- `kDeviceStateConnecting` - 正在连接
- `kDeviceStateActivating` - 正在激活

对话结束后，设备自动返回待机界面。

### 温湿度更新

- 待机模式下，每秒读取一次 DHT20 传感器
- 数据实时更新到屏幕上

---

## MCP 工具

AI 可以通过 MCP 协议读取温湿度数据：

**工具名称**: `sensor.read_temperature_humidity`

**功能**: 读取当前环境的温度和湿度

**返回数据**:
```json
{
  "temperature": 25.50,
  "humidity": 60.30
}
```

**使用示例**：
- 用户： "现在的温度和湿度是多少？"
- AI：[调用传感器] 根据传感器数据，当前室内温度是 **25.5°C**，湿度是 **60.3%**。

---

## 技术实现

### 新增文件

1. **main/display/standby_screen.h** - 待机界面头文件
2. **main/display/standby_screen.cc** - 待机界面实现

### 修改文件

1. **main/display/lcd_display.h** - 添加待机界面成员和函数
2. **main/display/lcd_display.cc** - 实现待机界面切换逻辑
3. **main/boards/bread-compact-wifi-lcd/compact_wifi_board_lcd.cc** - 集成 DHT20 和状态监控
4. **main/CMakeLists.txt** - 添加待机界面源文件

### 传感器驱动

**main/boards/common/dht20_sensor.h** - DHT20 传感器驱动（已存在）
- 头文件内嵌实现
- 使用 I2C 通信
- 自动读取温度和湿度

---

## 故障排查

### 问题 1: 屏幕显示乱码或空白

**原因**: 屏幕型号选择错误

**解决**:
1. 检查你的屏幕驱动芯片（ST7789、GC9A01、ILI9341 等）
2. 在 menuconfig 中选择正确的 LCD Type
3. 如果不确定，先试 `ST7789 240x240`

### 问题 2: 温湿度显示 `--.-°C` 和 `--.-%`

**原因**: DHT20 未正确连接或 I2C 通信失败

**解决**:
1. 检查硬件连接（特别是 SDA/SCL）
2. 确认使用 3.3V 供电（不要接 5V）
3. 检查串口日志中的错误信息

### 问题 3: 编译错误

**解决**:
```powershell
# 清理构建
idf.py fullclean

# 重新编译
idf.py build
```

### 问题 4: 待机界面不显示

**原因**: 设备未进入空闲状态

**解决**:
1. 检查网络连接是否正常
2. 等待设备启动完成
3. 查看串口日志中的设备状态

---

## 时间同步

设备会自动从网络同步时间（通过 SNTP）。

**首次使用**:
1. 连接 WiFi 后，设备会自动同步时间
2. 如果时间不正确，请检查网络连接

---

## 自定义显示

如果需要修改待机界面样式，可以编辑：
- `main/display/standby_screen.cc` - 修改布局和样式
- 颜色、字体、图标都可以自定义

---

## 注意事项

1. **电源**: DHT20 必须使用 3.3V 供电，接 5V 可能损坏传感器
2. **I2C 冲突**: DHT20 和 OLED 共享同一组 I2C 总线（GPIO 41/42），如果使用 OLED，可能需要调整 I2C 地址
3. **传感器响应**: DHT20 每次读取需要约 80ms，不要过于频繁读取
4. **屏幕亮度**: 可以通过小智语音指令调节屏幕亮度："把屏幕调亮一点"

---

## 许可证

本功能基于 xiaozhi-esp32 项目的 MIT 开源许可证。

---

## 反馈

如有问题或建议，欢迎在 GitHub 上提交 Issue 或 PR。
