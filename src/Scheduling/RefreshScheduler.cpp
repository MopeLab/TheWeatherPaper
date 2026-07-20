#include "RefreshScheduler.h"

#include <time.h>

namespace
{
String tomorrowDateKey(const WeatherData &weather)
{
    if (!weather.realtime.isAvailable())
    {
        return String();
    }

    constexpr int64_t OneDaySeconds = 24 * 60 * 60;
    const int64_t shiftedTomorrow =
        weather.realtime.serverTime +
        weather.realtime.tzShiftSeconds +
        OneDaySeconds;
    if (shiftedTomorrow <= 0)
    {
        return String();
    }

    const time_t timestamp = static_cast<time_t>(shiftedTomorrow);
    tm localTomorrow = {};
    if (gmtime_r(&timestamp, &localTomorrow) == nullptr)
    {
        return String();
    }

    char key[48];
    snprintf(
        key,
        sizeof(key),
        "%04d-%02d-%02d",
        localTomorrow.tm_year + 1900,
        localTomorrow.tm_mon + 1,
        localTomorrow.tm_mday);
    return String(key);
}

bool itemHasExactDate(const DailyWeatherItem &item, const String &dateKey)
{
    // 彩云 daily.date 可能是纯日期，也可能带 ISO 8601 时间和时区；
    // 只比较固定的前十个日期字符，不使用 startsWith 的模糊长度语义。
    return dateKey.length() == 10 &&
           item.date.length() >= 10 &&
           item.date.substring(0, 10) == dateKey;
}
} // namespace

namespace RefreshScheduler
{
bool isRainCondition(const String &skyCondition)
{
    return isRainCode(skyCondition.c_str());
}

bool weatherRequiresFastCadence(const WeatherData &weather)
{
    if (weather.realtime.isAvailable() &&
        isRainCondition(weather.realtime.skyCondition))
    {
        return true;
    }

    const String dateKey = tomorrowDateKey(weather);
    if (dateKey.isEmpty() || !weather.daily.isAvailable())
    {
        return false;
    }

    for (const DailyWeatherItem &item : weather.daily.items)
    {
        if (!itemHasExactDate(item, dateKey))
        {
            continue;
        }

        return isRainCondition(item.skyCondition) ||
               isRainCondition(item.daytimeSkyCondition) ||
               isRainCondition(item.nighttimeSkyCondition);
    }

    return false;
}
} // namespace RefreshScheduler

namespace
{
// 用一个“北京时间 00:00”对应的 Unix 秒作编译期测试基准。
constexpr int64_t BeijingMidnightEpoch =
    RefreshScheduler::SecondsPerDay -
    RefreshScheduler::BeijingUtcOffsetSeconds;

static_assert(RefreshScheduler::NormalIntervalSeconds == 600);
static_assert(RefreshScheduler::RainIntervalSeconds == 300);
static_assert(RefreshScheduler::MinimumRefreshIntervalSeconds == 180);
static_assert(RefreshScheduler::refreshIntervalSeconds(false) == 600);
static_assert(RefreshScheduler::refreshIntervalSeconds(true) == 300);
static_assert(RefreshScheduler::enforceMinimumRefreshInterval(1) == 180);
static_assert(RefreshScheduler::isRainCode("LIGHT_RAIN"));
static_assert(RefreshScheduler::isRainCode("MODERATE_RAIN"));
static_assert(RefreshScheduler::isRainCode("HEAVY_RAIN"));
static_assert(RefreshScheduler::isRainCode("STORM_RAIN"));
static_assert(!RefreshScheduler::isRainCode("CLOUDY"));
static_assert(!RefreshScheduler::isRainCode("LIGHT_SNOW"));

static_assert(!RefreshScheduler::isBeijingQuietHours(
    BeijingMidnightEpoch + 59 * 60));
static_assert(RefreshScheduler::isBeijingQuietHours(
    BeijingMidnightEpoch + 1 * 60 * 60));
static_assert(RefreshScheduler::isBeijingQuietHours(
    BeijingMidnightEpoch + 5 * 60 * 60 + 59 * 60));
static_assert(!RefreshScheduler::isBeijingQuietHours(
    BeijingMidnightEpoch + 6 * 60 * 60));

static_assert(
    RefreshScheduler::secondsUntilBeijingQuietEnd(
        BeijingMidnightEpoch + 5 * 60 * 60 + 59 * 60) ==
    60);
static_assert(
    RefreshScheduler::nextRefreshDelaySeconds(
        BeijingMidnightEpoch + 59 * 60,
        true) ==
    5 * 60 * 60 + 60);
static_assert(
    RefreshScheduler::nextRefreshDelaySeconds(
        BeijingMidnightEpoch + 59 * 60,
        false) ==
    5 * 60 * 60 + 60);
static_assert(
    RefreshScheduler::nextRefreshDelaySeconds(
        BeijingMidnightEpoch + 6 * 60 * 60,
        true) ==
    300);
} // namespace
