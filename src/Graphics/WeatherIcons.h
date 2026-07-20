#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

// 与彩云 skycon 一一对应的天气图标类型。图标由设备端 1-bit 图元绘制，
// 因而可以在 50×50 主天气区和 25×25 预报区使用同一套实现。
enum class WeatherIconKind : uint8_t
{
    ClearDay,
    ClearNight,
    PartlyCloudyDay,
    PartlyCloudyNight,
    Cloudy,
    LightHaze,
    ModerateHaze,
    HeavyHaze,
    LightRain,
    ModerateRain,
    HeavyRain,
    StormRain,
    Fog,
    LightSnow,
    ModerateSnow,
    HeavySnow,
    StormSnow,
    Dust,
    Sand,
    Wind,
    Unknown,
};

WeatherIconKind weatherIconKind(const String &skyCondition);
const char *weatherConditionText(const String &skyCondition);

// 在指定框内绘制不透明的 1-bit 天气图标。负宽高会先规范化，图标始终
// 居中并限制在框内；foreground/background 可用于黑白反转。
void drawWeatherIcon(
    Adafruit_GFX &target,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    const String &skyCondition,
    uint16_t foreground = 0x0000,
    uint16_t background = 0xFFFF);
