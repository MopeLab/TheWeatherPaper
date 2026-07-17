#pragma once

#include <vector>

#include "WeatherState.h"

// https://docs.caiyunapp.com/weather-api/v2/v2.6/3-hourly.html

// 将 API 中按指标分开的多个数组按 datetime 合并为单个逐小时条目，
// 避免 UI 自己维护多个数组下标。
struct HourlyWeatherItem
{
    // ISO 8601，例如 2022-05-26T16:00+08:00。
    String datetime;
    float temperature = NAN;
    float apparentTemperature = NAN;
    float humidity = NAN;
    float precipitation = NAN;
    // 降水概率，范围 0-100；-1 表示响应中没有该值。
    int16_t precipitationProbability = -1;
    WeatherWind wind;
    String skyCondition;
};

struct HourlyWeatherState : public WeatherState
{
    String description;
    std::vector<HourlyWeatherItem> items;

    bool isAvailable() const
    {
        return WeatherState::isAvailable() && !items.empty();
    }
};
