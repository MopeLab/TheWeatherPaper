#pragma once

#include "DailyWeatherState.h"
#include "HourlyWeatherState.h"
#include "RealtimeWeatherState.h"

// 一次综合天气请求形成的原子快照。API 层只会在三类数据都解析成功后
// 替换此对象，因此刷新失败时 UI 仍可继续使用上一份有效数据。
struct WeatherData
{
    RealtimeWeatherState realtime;
    HourlyWeatherState hourly;
    DailyWeatherState daily;
    String forecastKeypoint;

    bool isAvailable() const
    {
        return realtime.isAvailable() &&
               hourly.isAvailable() &&
               daily.isAvailable();
    }

    void invalidate()
    {
        realtime.invalidate();
        hourly.invalidate();
        daily.invalidate();
    }
};
