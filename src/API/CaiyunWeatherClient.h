#pragma once

#include <Arduino.h>

#include "DataModel/WeatherData.h"

enum class WeatherFetchStatus : uint8_t
{
    Success,
    InvalidConfiguration,
    WifiUnavailable,
    ClockUnavailable,
    TlsOrHttpError,
    AuthenticationRejected,
    RateLimited,
    ApiRejected,
    JsonInvalid,
    DataInvalid
};

const char *weatherFetchStatusText(WeatherFetchStatus status);

struct WeatherFetchResult
{
    WeatherFetchStatus status = WeatherFetchStatus::DataInvalid;
    int httpStatus = 0;
    uint8_t attempts = 0;
    uint32_t elapsedMs = 0;

    bool succeeded() const
    {
        return status == WeatherFetchStatus::Success;
    }
};

enum class ClockSyncStatus : uint8_t
{
    Success,
    InvalidConfiguration,
    WifiUnavailable,
    NtpUnavailable
};

struct ClockSyncResult
{
    ClockSyncStatus status = ClockSyncStatus::NtpUnavailable;
    bool networkSyncCompleted = false;
    uint32_t elapsedMs = 0;

    bool succeeded() const
    {
        return status == ClockSyncStatus::Success;
    }
};

struct CaiyunWeatherClientOptions
{
    uint16_t hourlySteps = 24;
    uint8_t dailySteps = 7;
    uint8_t maxAttempts = 3;
    uint32_t wifiConnectTimeoutMs = 12000;
    uint32_t clockSyncTimeoutMs = 10000;
    uint16_t httpConnectTimeoutMs = 8000;
    uint16_t httpReadTimeoutMs = 15000;
    bool disconnectWifiAfterRequest = true;
};

// 同步、低频调用的天气 API 客户端。刷新层负责决定何时调用 refresh()；
// 本类负责临时联网、认证、请求、重试、解析以及成功后的原子数据替换。
class CaiyunWeatherClient
{
public:
    explicit CaiyunWeatherClient(
        Print &logger,
        const CaiyunWeatherClientOptions &options = CaiyunWeatherClientOptions());

    // 仅连接 Wi-Fi 并校准系统时钟，不请求天气。冷启动必须先用它取得
    // 可信时间，才能严格执行 01:00--06:00 的静默时段。
    ClockSyncResult synchronizeClock(bool forceNetworkSync = false);

    WeatherFetchResult refresh(WeatherData &target);

private:
    Print &logger_;
    CaiyunWeatherClientOptions options_;
};
