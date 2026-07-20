#pragma once

#include <math.h>

// 室内传感器形成的独立值对象。湿度使用 0-100 百分比，气压使用 Pa，
// 不与彩云天气模型中 0.0-1.0 的湿度范围混用。
struct IndoorEnvironmentState
{
    bool valid = false;
    float temperatureC = NAN;
    float humidityPercent = NAN;
    float pressurePa = NAN;

    bool isAvailable() const
    {
        return valid &&
               isfinite(temperatureC) &&
               isfinite(humidityPercent) &&
               temperatureC >= -40.0f &&
               temperatureC <= 85.0f &&
               humidityPercent >= 0.0f &&
               humidityPercent <= 100.0f;
    }

    bool hasPressure() const
    {
        return valid &&
               isfinite(pressurePa) &&
               pressurePa >= 30000.0f &&
               pressurePa <= 110000.0f;
    }

    // 保留原有两参数入口，便于未来接入只有温湿度的传感器；此时
    // 本站气压会由 UI 自动回退到天气 API。
    void update(float newTemperatureC, float newHumidityPercent)
    {
        temperatureC = newTemperatureC;
        humidityPercent = newHumidityPercent;
        pressurePa = NAN;
        valid = true;
        valid = isAvailable();
    }

    void update(
        float newTemperatureC,
        float newHumidityPercent,
        float newPressurePa)
    {
        temperatureC = newTemperatureC;
        humidityPercent = newHumidityPercent;
        pressurePa = newPressurePa;
        valid = true;
        valid = isAvailable();
    }

    void invalidate()
    {
        valid = false;
    }
};
