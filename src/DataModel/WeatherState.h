#pragma once

#include <Arduino.h>
#include <math.h>

// 各天气模型共用的响应元数据。
// serverTime 对应彩云响应根节点的 server_time（Unix 秒），tzShiftSeconds
// 对应 tzshift。UI 可用二者显示接口返回时当地的当前时间。
struct WeatherState
{
    bool valid = false;
    int64_t serverTime = 0;
    int32_t tzShiftSeconds = 0;

    bool isAvailable() const
    {
        return valid && serverTime > 0;
    }

    void markAvailable(int64_t responseServerTime, int32_t responseTzShiftSeconds)
    {
        serverTime = responseServerTime;
        tzShiftSeconds = responseTzShiftSeconds;
        valid = responseServerTime > 0;
    }

    // 请求或解析失败时只改变可用状态，保留旧值以便调试。
    void invalidate()
    {
        valid = false;
    }
};

struct WeatherWind
{
    // 彩云 metric 单位制下为 km/h。
    float speed = NAN;
    // 正北为 0 度，顺时针增加。
    float direction = NAN;
};
