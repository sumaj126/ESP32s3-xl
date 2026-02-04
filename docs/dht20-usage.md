# DHT20 温湿度传感器集成指南

## 硬件连接

DHT20 传感器使用 I2C 协议，复用 OLED 显示的 I2C 总线：

| DHT20 引脚 | ESP32-S3 引脚 | 说明 |
|-----------|--------------|------|
| VCC | 3.3V | 电源 |
| GND | GND | 地 |
| SDA | GPIO 41 | I2C 数据线 |
| SCL | GPIO 42 | I2C 时钟线 |

**注意**: DHT20 传感器需要 3.3V 供电，不要接 5V！

## 功能特性

通过 MCP 协议，AI 可以读取当前的温度和湿度数据。

### MCP 工具

- **工具名称**: `sensor.read_temperature_humidity`
- **功能**: 读取当前环境的温度和湿度
- **参数**: 无
- **返回值**: JSON 格式数据

```json
{
  "temperature": 25.50,
  "humidity": 60.30
}
```

## 使用示例

### AI 对话示例

**用户**: 现在室内温度是多少？

**AI**: [调用 MCP 工具 `sensor.read_temperature_humidity`]
根据传感器数据，当前室内温度是 **25.5°C**，湿度是 **60.3%**。

**用户**: 湿度太高了，能帮我看下吗？

**AI**: 当前室内湿度为 60.3%，这个湿度水平在正常范围内（40%-70%）。如果感觉潮湿，可以考虑：
1. 开启除湿机
2. 保持室内通风
3. 查看是否需要降低湿度

### 程序调用示例

在服务器端调用 DHT20 传感器：

```python
import json

# 通过 MCP 协议调用工具
result = {
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
        "name": "sensor.read_temperature_humidity",
        "arguments": {}
    },
    "id": 1
}

# 发送到设备后，返回结果：
# {"jsonrpc":"2.0","result":{"content":[{"type":"text","text":"{\"temperature\": 25.50, \"humidity\": 60.30}"}],"isError":false},"id":1}

# 解析结果
data = json.loads(result["result"]["content"][0]["text"])
print(f"温度: {data['temperature']}°C")
print(f"湿度: {data['humidity']}%")
```

## 技术细节

### DHT20 规格参数

- **工作电压**: 2.0V - 5.5V
- **工作电流**: 0.5mA（测量时）
- **测量范围**:
  - 温度: -40°C ~ 80°C
  - 湿度: 0% ~ 100% RH
- **测量精度**:
  - 温度: ±0.5°C
  - 湿度: ±3% RH
- **响应时间**: 约 80ms

### I2C 地址

DHT20 的默认 I2C 地址是 `0x38`，已在配置文件中定义：

```c
#define DHT20_I2C_ADDR 0x38
```

### 复用 I2C 总线

DHT20 传感器与 OLED 显示屏共享同一个 I2C 总线（I2C0），这样可以节省 GPIO 资源。

## 故障排查

### 1. 传感器读取失败

如果日志显示 `Failed to read DHT20`，检查：
- I2C 连接线是否正确
- 传感器是否供电（3.3V）
- 传感器是否损坏

### 2. 数值异常

如果读取到的温度或湿度值明显不正常：
- 检查传感器是否受潮
- 确认工作环境温度是否在测量范围内
- 重新校准传感器

### 3. 编译错误

确保新添加的头文件在正确的路径：
- `dht20_sensor.h` 应在 `main/boards/common/` 目录下

## 扩展建议

可以进一步扩展功能：

1. **定时上报**: 每隔一段时间自动上报温湿度数据
2. **阈值报警**: 温度或湿度超过阈值时触发报警
3. **数据记录**: 在 Flash 中记录历史数据
4. **多传感器**: 支持多个 DHT20 传感器

## 相关文件

- `main/boards/common/dht20_sensor.h` - DHT20 驱动实现
- `main/boards/bread-compact-wifi/config.h` - 引脚配置
- `main/boards/bread-compact-wifi/compact_wifi_board.cc` - 开发板实现
- `docs/mcp-usage.md` - MCP 协议使用说明
