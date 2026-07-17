#pragma once

#include <vector>

#include "WeatherState.h"

// https://docs.caiyunapp.com/weather-api/v2/v2.6/4-daily.html

// 将 API 中 temperature、skycon、astro 等并行数组按 date 合并，
// UI 可直接遍历未来若干天。
struct DailyWeatherItem
{
    // ISO 8601，例如 2022-05-26T00:00+08:00。
    String date;
    float minimumTemperature = NAN;
    float maximumTemperature = NAN;
    // 全天降水概率，范围 0-100；-1 表示响应中没有该值。
    int16_t precipitationProbability = -1;

    String skyCondition;
    String daytimeSkyCondition;
    String nighttimeSkyCondition;
    String sunriseTime;
    String sunsetTime;

    WeatherWind averageWind;
    float averageHumidity = NAN;
    int16_t averageAqi = -1;
    int16_t averagePm25 = -1;

    String ultravioletDescription;
    String dressingDescription;
    String comfortDescription;
};

struct DailyWeatherState : public WeatherState
{
    std::vector<DailyWeatherItem> items;

    bool isAvailable() const
    {
        return WeatherState::isAvailable() && !items.empty();
    }
};
