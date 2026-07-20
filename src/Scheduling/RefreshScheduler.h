#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "../DataModel/WeatherData.h"

// 天气页面的定时刷新策略。
//
// 本模块只负责计算策略，不启动定时器、不操作屏幕，也不进入休眠；因此
// main 可以用同一组接口驱动深睡定时唤醒或常规 millis() 后备定时器。
namespace RefreshScheduler
{
    constexpr int64_t NormalIntervalSeconds = 10 * 60;
    constexpr int64_t RainIntervalSeconds = 5 * 60;
    constexpr int64_t MinimumRefreshIntervalSeconds = 3 * 60;

    constexpr int32_t BeijingUtcOffsetSeconds = 8 * 60 * 60;
    constexpr int32_t SecondsPerDay = 24 * 60 * 60;
    constexpr int32_t QuietStartSecondOfDay = 1 * 60 * 60; // 01:00
    constexpr int32_t QuietEndSecondOfDay = 6 * 60 * 60;   // 06:00

    constexpr bool textEquals(const char *left, const char *right)
    {
        if (left == nullptr || right == nullptr)
        {
            return left == right;
        }
        while (*left != '\0' && *right != '\0')
        {
            if (*left != *right)
            {
                return false;
            }
            ++left;
            ++right;
        }
        return *left == *right;
    }

    constexpr bool isRainCode(const char *skyCondition)
    {
        return textEquals(skyCondition, "LIGHT_RAIN") ||
               textEquals(skyCondition, "MODERATE_RAIN") ||
               textEquals(skyCondition, "HEAVY_RAIN") ||
               textEquals(skyCondition, "STORM_RAIN");
    }

    // 对负 Unix 时间也返回 [0, 86400)；实际固件只会传入已校时的正时间。
    constexpr int32_t positiveModulo(int64_t value, int32_t divisor)
    {
        const int32_t remainder = static_cast<int32_t>(value % divisor);
        return remainder < 0 ? remainder + divisor : remainder;
    }

    constexpr int32_t beijingSecondOfDay(int64_t unixEpochSeconds)
    {
        return positiveModulo(
            unixEpochSeconds + BeijingUtcOffsetSeconds,
            SecondsPerDay);
    }

    // 静默区间采用半开区间 [01:00, 06:00)。
    constexpr bool isBeijingQuietHours(int64_t unixEpochSeconds)
    {
        const int32_t secondOfDay = beijingSecondOfDay(unixEpochSeconds);
        return secondOfDay >= QuietStartSecondOfDay &&
               secondOfDay < QuietEndSecondOfDay;
    }

    // 仅在当前位于静默时段时使用；结果会精确指向当日北京时间 06:00。
    // 临近 06:00 时该等待可能短于 180 秒，但它不会触发屏幕刷新，只负责
    // 退出静默，因此不违反电子纸两次实际刷新至少间隔 180 秒的约束。
    constexpr int64_t secondsUntilBeijingQuietEnd(int64_t unixEpochSeconds)
    {
        const int32_t secondOfDay = beijingSecondOfDay(unixEpochSeconds);
        return secondOfDay >= QuietStartSecondOfDay &&
                       secondOfDay < QuietEndSecondOfDay
                   ? QuietEndSecondOfDay - secondOfDay
                   : 0;
    }

    constexpr int64_t enforceMinimumRefreshInterval(int64_t requestedSeconds)
    {
        return requestedSeconds < MinimumRefreshIntervalSeconds
                   ? MinimumRefreshIntervalSeconds
                   : requestedSeconds;
    }

    constexpr int64_t refreshIntervalSeconds(bool rainCadence)
    {
        return enforceMinimumRefreshInterval(
            rainCadence ? RainIntervalSeconds : NormalIntervalSeconds);
    }

    // 从 now 起计算下一次允许进行天气请求和屏幕刷新的等待秒数。
    // 当前在静默期，或正常间隔的落点进入静默期时，均钳制到北京时间 06:00。
    constexpr int64_t nextRefreshDelaySeconds(
        int64_t nowUnixEpochSeconds,
        bool rainCadence)
    {
        const int64_t quietRemainder =
            secondsUntilBeijingQuietEnd(nowUnixEpochSeconds);
        if (quietRemainder > 0)
        {
            return quietRemainder;
        }

        const int64_t interval = refreshIntervalSeconds(rainCadence);
        const int64_t candidate = nowUnixEpochSeconds + interval;
        const int64_t candidateQuietRemainder =
            secondsUntilBeijingQuietEnd(candidate);

        return candidateQuietRemainder > 0
                   ? interval + candidateQuietRemainder
                   : interval;
    }

    // 返回绝对 Unix 时间，便于保存到 RTC_DATA_ATTR；它和上面的 delay API
    // 使用同一套规则，避免 main 重复实现静默期判断。
    constexpr int64_t nextRefreshEpochSeconds(
        int64_t nowUnixEpochSeconds,
        bool rainCadence)
    {
        return nowUnixEpochSeconds +
               nextRefreshDelaySeconds(nowUnixEpochSeconds, rainCadence);
    }

    // 仅识别产品需求中明确列出的四种雨类，避免把雪、雾或降水概率误判为雨。
    bool isRainCondition(const String &skyCondition);

    // 当前实况或“明天”的全天、日间、夜间任一现象为雨时返回 true。
    // 明天通过 serverTime + tzShiftSeconds 生成 YYYY-MM-DD 后精确匹配，
    // 不依赖 daily.items 的数组位置或排序。
    bool weatherRequiresFastCadence(const WeatherData &weather);
} // namespace RefreshScheduler
