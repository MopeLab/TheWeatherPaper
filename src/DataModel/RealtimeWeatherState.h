#pragma once

#include "WeatherState.h"

// https://docs.caiyunapp.com/weather-api/v2/v2.6/1-realtime.html

// 彩云 $.result.realtime 中适合天气主页面展示的实况数据。
struct RealtimeWeatherState : public WeatherState
{
    float temperature = NAN;
    float apparentTemperature = NAN;
    // 相对湿度，范围 0.0-1.0。
    float humidity = NAN;
    // 本地降水强度，metric 单位制下为 mm/h。
    float precipitationIntensity = NAN;
    float pressure = NAN;
    float visibility = NAN;
    WeatherWind wind;
    String skyCondition;

    int16_t aqi = -1;
    int16_t pm25 = -1;
    String comfortDescription;
    String ultravioletDescription;

    bool isAvailable() const
    {
        return WeatherState::isAvailable() &&
               !isnan(temperature) &&
               skyCondition.length() > 0;
    }
};
