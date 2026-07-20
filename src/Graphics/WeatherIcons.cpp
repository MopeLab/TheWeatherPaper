#include "WeatherIcons.h"

#include <algorithm>
#include <stdlib.h>

namespace
{
constexpr int16_t LogicalMaximum = 47;

class IconCanvas
{
public:
    IconCanvas(
        Adafruit_GFX &target,
        int16_t left,
        int16_t top,
        uint16_t size,
        uint16_t foreground,
        uint16_t background)
        : target_(target),
          left_(left),
          top_(top),
          size_(std::max<uint16_t>(1, size)),
          foreground_(foreground),
          background_(background)
    {
    }

    int16_t x(int16_t logical) const
    {
        const int16_t clamped = std::max<int16_t>(
            0,
            std::min<int16_t>(LogicalMaximum, logical));
        return left_ + static_cast<int32_t>(clamped) * (size_ - 1) /
                           LogicalMaximum;
    }

    int16_t y(int16_t logical) const
    {
        const int16_t clamped = std::max<int16_t>(
            0,
            std::min<int16_t>(LogicalMaximum, logical));
        return top_ + static_cast<int32_t>(clamped) * (size_ - 1) /
                          LogicalMaximum;
    }

    uint16_t length(uint16_t logical) const
    {
        return std::max<uint16_t>(
            1,
            (static_cast<uint32_t>(logical) * size_ + 23) / 48);
    }

    void line(
        int16_t x1,
        int16_t y1,
        int16_t x2,
        int16_t y2,
        uint8_t logicalThickness = 1)
    {
        const int16_t actualX1 = x(x1);
        const int16_t actualY1 = y(y1);
        const int16_t actualX2 = x(x2);
        const int16_t actualY2 = y(y2);
        const int16_t thickness = length(logicalThickness);
        const int16_t firstOffset = -(thickness - 1) / 2;
        const bool mostlyHorizontal =
            abs(actualX2 - actualX1) >= abs(actualY2 - actualY1);

        for (int16_t index = 0; index < thickness; ++index)
        {
            const int16_t offset = firstOffset + index;
            target_.drawLine(
                actualX1 + (mostlyHorizontal ? 0 : offset),
                actualY1 + (mostlyHorizontal ? offset : 0),
                actualX2 + (mostlyHorizontal ? 0 : offset),
                actualY2 + (mostlyHorizontal ? offset : 0),
                foreground_);
        }
    }

    void fillCircle(int16_t centerX, int16_t centerY, uint8_t radius)
    {
        target_.fillCircle(
            x(centerX),
            y(centerY),
            length(radius),
            foreground_);
    }

    void eraseCircle(int16_t centerX, int16_t centerY, uint8_t radius)
    {
        target_.fillCircle(
            x(centerX),
            y(centerY),
            length(radius),
            background_);
    }

    void circle(
        int16_t centerX,
        int16_t centerY,
        uint8_t radius,
        uint8_t logicalThickness = 1)
    {
        const uint16_t actualRadius = length(radius);
        const uint16_t thickness = std::min<uint16_t>(
            actualRadius,
            length(logicalThickness));
        for (uint16_t index = 0; index < thickness; ++index)
        {
            target_.drawCircle(
                x(centerX),
                y(centerY),
                actualRadius - index,
                foreground_);
        }
    }

    void fillRoundRect(
        int16_t left,
        int16_t top,
        int16_t right,
        int16_t bottom,
        uint8_t radius)
    {
        const int16_t actualLeft = x(left);
        const int16_t actualTop = y(top);
        const int16_t actualRight = x(right);
        const int16_t actualBottom = y(bottom);
        target_.fillRoundRect(
            actualLeft,
            actualTop,
            std::max<int16_t>(1, actualRight - actualLeft + 1),
            std::max<int16_t>(1, actualBottom - actualTop + 1),
            length(radius),
            foreground_);
    }

    void fillTriangle(
        int16_t x1,
        int16_t y1,
        int16_t x2,
        int16_t y2,
        int16_t x3,
        int16_t y3)
    {
        target_.fillTriangle(
            x(x1),
            y(y1),
            x(x2),
            y(y2),
            x(x3),
            y(y3),
            foreground_);
    }

private:
    Adafruit_GFX &target_;
    int16_t left_;
    int16_t top_;
    uint16_t size_;
    uint16_t foreground_;
    uint16_t background_;
};

void drawSunAt(
    IconCanvas &canvas,
    int16_t centerX,
    int16_t centerY,
    uint8_t radius,
    uint8_t outerRadius)
{
    canvas.circle(centerX, centerY, radius, 2);
    const int16_t inner = radius + 3;
    const int16_t outer = outerRadius;
    canvas.line(centerX, centerY - inner, centerX, centerY - outer, 2);
    canvas.line(centerX, centerY + inner, centerX, centerY + outer, 2);
    canvas.line(centerX - inner, centerY, centerX - outer, centerY, 2);
    canvas.line(centerX + inner, centerY, centerX + outer, centerY, 2);

    const int16_t diagonalInner = radius + 2;
    const int16_t diagonalOuter = outerRadius - 2;
    canvas.line(
        centerX - diagonalInner,
        centerY - diagonalInner,
        centerX - diagonalOuter,
        centerY - diagonalOuter,
        2);
    canvas.line(
        centerX + diagonalInner,
        centerY - diagonalInner,
        centerX + diagonalOuter,
        centerY - diagonalOuter,
        2);
    canvas.line(
        centerX - diagonalInner,
        centerY + diagonalInner,
        centerX - diagonalOuter,
        centerY + diagonalOuter,
        2);
    canvas.line(
        centerX + diagonalInner,
        centerY + diagonalInner,
        centerX + diagonalOuter,
        centerY + diagonalOuter,
        2);
}

void drawSun(IconCanvas &canvas)
{
    drawSunAt(canvas, 24, 24, 8, 20);
}

void drawMoonAt(
    IconCanvas &canvas,
    int16_t centerX,
    int16_t centerY,
    uint8_t radius)
{
    canvas.fillCircle(centerX, centerY, radius);
    canvas.eraseCircle(centerX + radius / 2, centerY - radius / 2, radius - 2);
}

void drawMoon(IconCanvas &canvas)
{
    drawMoonAt(canvas, 23, 24, 13);
}

void drawCloud(IconCanvas &canvas, int16_t verticalShift = 0)
{
    canvas.fillCircle(16, 24 + verticalShift, 7);
    canvas.fillCircle(25, 20 + verticalShift, 10);
    canvas.fillCircle(35, 25 + verticalShift, 7);
    canvas.fillRoundRect(
        9,
        24 + verticalShift,
        41,
        35 + verticalShift,
        5);
}

void drawPartlyCloudy(IconCanvas &canvas, bool night)
{
    if (night)
    {
        drawMoonAt(canvas, 15, 15, 8);
    }
    else
    {
        drawSunAt(canvas, 15, 15, 5, 11);
    }
    drawCloud(canvas, 3);
}

void drawRainDrop(
    IconCanvas &canvas,
    int16_t x,
    int16_t y,
    uint8_t thickness = 2)
{
    canvas.line(x + 2, y, x - 2, y + 7, thickness);
}

void drawRain(IconCanvas &canvas, uint8_t intensity)
{
    drawCloud(canvas, -7);
    if (intensity == 1)
    {
        drawRainDrop(canvas, 19, 33, 2);
        drawRainDrop(canvas, 31, 33, 2);
        return;
    }

    drawRainDrop(canvas, 14, 32, intensity >= 3 ? 3 : 2);
    drawRainDrop(canvas, 25, 32, intensity >= 3 ? 3 : 2);
    drawRainDrop(canvas, 36, 32, intensity >= 3 ? 3 : 2);
    if (intensity >= 3)
    {
        drawRainDrop(canvas, 8, 36, 2);
        drawRainDrop(canvas, 30, 37, 2);
        drawRainDrop(canvas, 43, 36, 2);
    }
    if (intensity >= 4)
    {
        canvas.line(19, 36, 15, 45, 3);
        canvas.line(39, 32, 35, 43, 3);
    }
}

void drawSnowflake(
    IconCanvas &canvas,
    int16_t centerX,
    int16_t centerY,
    uint8_t radius = 3)
{
    canvas.line(centerX - radius, centerY, centerX + radius, centerY);
    canvas.line(centerX, centerY - radius, centerX, centerY + radius);
    canvas.line(
        centerX - radius + 1,
        centerY - radius + 1,
        centerX + radius - 1,
        centerY + radius - 1);
    canvas.line(
        centerX + radius - 1,
        centerY - radius + 1,
        centerX - radius + 1,
        centerY + radius - 1);
}

void drawSnow(IconCanvas &canvas, uint8_t intensity)
{
    drawCloud(canvas, -7);
    if (intensity == 1)
    {
        drawSnowflake(canvas, 18, 37, 3);
        drawSnowflake(canvas, 32, 37, 3);
        return;
    }

    drawSnowflake(canvas, 13, 36, 3);
    drawSnowflake(canvas, 25, 39, 3);
    drawSnowflake(canvas, 37, 36, 3);
    if (intensity >= 3)
    {
        drawSnowflake(canvas, 8, 44, 2);
        drawSnowflake(canvas, 42, 44, 2);
    }
    if (intensity >= 4)
    {
        drawSnowflake(canvas, 19, 45, 2);
        drawSnowflake(canvas, 32, 45, 2);
    }
}

void drawHaze(IconCanvas &canvas, uint8_t intensity)
{
    drawSunAt(canvas, 13, 13, 4, 9);
    canvas.line(7, 23, 41, 23, intensity >= 3 ? 3 : 2);
    canvas.line(12, 30, 44, 30, intensity >= 2 ? 2 : 1);
    canvas.line(5, 37, 36, 37, intensity >= 2 ? 2 : 1);
    if (intensity >= 2)
    {
        canvas.line(16, 44, 42, 44, intensity >= 3 ? 3 : 2);
        canvas.fillCircle(7, 30, 1);
    }
    if (intensity >= 3)
    {
        canvas.fillCircle(42, 37, 2);
        canvas.fillCircle(9, 44, 2);
    }
}

void drawFog(IconCanvas &canvas)
{
    drawCloud(canvas, -9);
    canvas.line(5, 31, 36, 31, 2);
    canvas.line(13, 37, 43, 37, 2);
    canvas.line(6, 43, 34, 43, 2);
}

void drawDust(IconCanvas &canvas)
{
    canvas.fillCircle(11, 12, 2);
    canvas.fillCircle(29, 8, 1);
    canvas.fillCircle(39, 17, 2);
    canvas.fillCircle(18, 27, 1);
    canvas.fillCircle(35, 35, 1);
    canvas.line(5, 20, 29, 20, 2);
    canvas.line(29, 20, 36, 17, 2);
    canvas.line(8, 32, 28, 32, 2);
    canvas.line(28, 32, 38, 27, 2);
    canvas.line(5, 42, 29, 42, 2);
}

void drawSand(IconCanvas &canvas)
{
    canvas.line(5, 11, 31, 11, 2);
    canvas.line(31, 11, 39, 16, 2);
    canvas.line(10, 21, 39, 21, 2);
    canvas.fillCircle(7, 27, 1);
    canvas.fillCircle(18, 25, 2);
    canvas.fillCircle(34, 29, 1);
    canvas.fillTriangle(4, 45, 19, 32, 31, 45);
    canvas.fillTriangle(20, 45, 34, 36, 44, 45);
}

void drawWind(IconCanvas &canvas)
{
    canvas.line(5, 13, 31, 13, 2);
    canvas.line(31, 13, 37, 9, 2);
    canvas.line(37, 9, 41, 12, 2);
    canvas.line(41, 12, 38, 17, 2);

    canvas.line(8, 25, 39, 25, 2);
    canvas.line(39, 25, 43, 29, 2);
    canvas.line(43, 29, 40, 33, 2);

    canvas.line(5, 38, 28, 38, 2);
    canvas.line(28, 38, 34, 34, 2);
}

void drawUnknown(IconCanvas &canvas)
{
    canvas.circle(24, 24, 17, 2);
    canvas.line(17, 17, 20, 13, 2);
    canvas.line(20, 13, 28, 13, 2);
    canvas.line(28, 13, 32, 17, 2);
    canvas.line(32, 17, 32, 21, 2);
    canvas.line(32, 21, 24, 28, 2);
    canvas.line(24, 28, 24, 32, 2);
    canvas.fillCircle(24, 38, 2);
}

void drawIcon(IconCanvas &canvas, WeatherIconKind kind)
{
    switch (kind)
    {
    case WeatherIconKind::ClearDay: drawSun(canvas); break;
    case WeatherIconKind::ClearNight: drawMoon(canvas); break;
    case WeatherIconKind::PartlyCloudyDay: drawPartlyCloudy(canvas, false); break;
    case WeatherIconKind::PartlyCloudyNight: drawPartlyCloudy(canvas, true); break;
    case WeatherIconKind::Cloudy: drawCloud(canvas); break;
    case WeatherIconKind::LightHaze: drawHaze(canvas, 1); break;
    case WeatherIconKind::ModerateHaze: drawHaze(canvas, 2); break;
    case WeatherIconKind::HeavyHaze: drawHaze(canvas, 3); break;
    case WeatherIconKind::LightRain: drawRain(canvas, 1); break;
    case WeatherIconKind::ModerateRain: drawRain(canvas, 2); break;
    case WeatherIconKind::HeavyRain: drawRain(canvas, 3); break;
    case WeatherIconKind::StormRain: drawRain(canvas, 4); break;
    case WeatherIconKind::Fog: drawFog(canvas); break;
    case WeatherIconKind::LightSnow: drawSnow(canvas, 1); break;
    case WeatherIconKind::ModerateSnow: drawSnow(canvas, 2); break;
    case WeatherIconKind::HeavySnow: drawSnow(canvas, 3); break;
    case WeatherIconKind::StormSnow: drawSnow(canvas, 4); break;
    case WeatherIconKind::Dust: drawDust(canvas); break;
    case WeatherIconKind::Sand: drawSand(canvas); break;
    case WeatherIconKind::Wind: drawWind(canvas); break;
    case WeatherIconKind::Unknown: drawUnknown(canvas); break;
    }
}
}

WeatherIconKind weatherIconKind(const String &skyCondition)
{
    if (skyCondition == "CLEAR_DAY") return WeatherIconKind::ClearDay;
    if (skyCondition == "CLEAR_NIGHT") return WeatherIconKind::ClearNight;
    if (skyCondition == "PARTLY_CLOUDY_DAY") return WeatherIconKind::PartlyCloudyDay;
    if (skyCondition == "PARTLY_CLOUDY_NIGHT") return WeatherIconKind::PartlyCloudyNight;
    if (skyCondition == "CLOUDY") return WeatherIconKind::Cloudy;
    if (skyCondition == "LIGHT_HAZE") return WeatherIconKind::LightHaze;
    if (skyCondition == "MODERATE_HAZE") return WeatherIconKind::ModerateHaze;
    if (skyCondition == "HEAVY_HAZE") return WeatherIconKind::HeavyHaze;
    if (skyCondition == "LIGHT_RAIN") return WeatherIconKind::LightRain;
    if (skyCondition == "MODERATE_RAIN") return WeatherIconKind::ModerateRain;
    if (skyCondition == "HEAVY_RAIN") return WeatherIconKind::HeavyRain;
    if (skyCondition == "STORM_RAIN") return WeatherIconKind::StormRain;
    if (skyCondition == "FOG") return WeatherIconKind::Fog;
    if (skyCondition == "LIGHT_SNOW") return WeatherIconKind::LightSnow;
    if (skyCondition == "MODERATE_SNOW") return WeatherIconKind::ModerateSnow;
    if (skyCondition == "HEAVY_SNOW") return WeatherIconKind::HeavySnow;
    if (skyCondition == "STORM_SNOW") return WeatherIconKind::StormSnow;
    if (skyCondition == "DUST") return WeatherIconKind::Dust;
    if (skyCondition == "SAND") return WeatherIconKind::Sand;
    if (skyCondition == "WIND") return WeatherIconKind::Wind;
    return WeatherIconKind::Unknown;
}

const char *weatherConditionText(const String &skyCondition)
{
    if (skyCondition == "CLEAR_DAY" || skyCondition == "CLEAR_NIGHT") return "晴";
    if (skyCondition == "PARTLY_CLOUDY_DAY" ||
        skyCondition == "PARTLY_CLOUDY_NIGHT") return "多云";
    if (skyCondition == "CLOUDY") return "阴";
    if (skyCondition == "LIGHT_HAZE") return "轻度雾霾";
    if (skyCondition == "MODERATE_HAZE") return "中度雾霾";
    if (skyCondition == "HEAVY_HAZE") return "重度雾霾";
    if (skyCondition == "LIGHT_RAIN") return "小雨";
    if (skyCondition == "MODERATE_RAIN") return "中雨";
    if (skyCondition == "HEAVY_RAIN") return "大雨";
    if (skyCondition == "STORM_RAIN") return "暴雨";
    if (skyCondition == "FOG") return "雾";
    if (skyCondition == "LIGHT_SNOW") return "小雪";
    if (skyCondition == "MODERATE_SNOW") return "中雪";
    if (skyCondition == "HEAVY_SNOW") return "大雪";
    if (skyCondition == "STORM_SNOW") return "暴雪";
    if (skyCondition == "DUST") return "浮尘";
    if (skyCondition == "SAND") return "沙尘";
    if (skyCondition == "WIND") return "大风";
    return "未知";
}

void drawWeatherIcon(
    Adafruit_GFX &target,
    int16_t x,
    int16_t y,
    int16_t width,
    int16_t height,
    const String &skyCondition,
    uint16_t foreground,
    uint16_t background)
{
    const int16_t left = width >= 0 ? x : x + width;
    const int16_t top = height >= 0 ? y : y + height;
    const uint16_t boxWidth = std::max<int16_t>(1, abs(width));
    const uint16_t boxHeight = std::max<int16_t>(1, abs(height));
    const uint16_t size = std::min(boxWidth, boxHeight);
    const int16_t iconLeft = left + (boxWidth - size) / 2;
    const int16_t iconTop = top + (boxHeight - size) / 2;

    target.fillRect(left, top, boxWidth, boxHeight, background);
    IconCanvas canvas(
        target,
        iconLeft,
        iconTop,
        size,
        foreground,
        background);
    drawIcon(canvas, weatherIconKind(skyCondition));
}
