#include "CaiyunWeatherClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <time.h>
#include <utility>

#include "CaiyunCertificates.h"
#include "secrets.h"

namespace
{
constexpr char ApiHost[] = "api.caiyunapp.com";
constexpr char ApiVersion[] = "v2.6";
constexpr int64_t MinimumValidEpoch = 1704067200; // 2024-01-01 UTC
constexpr uint32_t BaseRetryDelayMs = 750;
constexpr uint32_t MaximumRetryDelayMs = 5000;
constexpr int MaximumResponseBytes = 256 * 1024;

struct AttemptResult
{
    WeatherFetchStatus status = WeatherFetchStatus::TlsOrHttpError;
    int httpStatus = 0;
    uint32_t retryAfterMs = 0;
    bool retryable = false;

    AttemptResult() = default;

    AttemptResult(
        WeatherFetchStatus attemptStatus,
        int attemptHttpStatus,
        uint32_t attemptRetryAfterMs,
        bool canRetry)
        : status(attemptStatus),
          httpStatus(attemptHttpStatus),
          retryAfterMs(attemptRetryAfterMs),
          retryable(canRetry)
    {
    }
};

bool hasPlaceholder(const char *value)
{
    return value == nullptr ||
           value[0] == '\0' ||
           strstr(value, "替换为") != nullptr ||
           strcmp(value, "TOKEN") == 0 ||
           strcmp(value, "APP_KEY") == 0 ||
           strcmp(value, "APP_SECRET") == 0;
}

bool credentialIsValid(const char *value)
{
    // 彩云正式凭证远长于这些占位文本；提前拒绝可避免无意义的联网和重试。
    return !hasPlaceholder(value) && strlen(value) >= 8;
}

bool configurationIsValid(const CaiyunWeatherClientOptions &options)
{
    if (hasPlaceholder(WIFI_SSID) ||
        hasPlaceholder(LOCATION_LONGITUDE) ||
        hasPlaceholder(LOCATION_LATITUDE))
    {
        return false;
    }

    if (CAIYUN_USE_SIGNED_AUTH)
    {
        if (!credentialIsValid(CAIYUN_APP_KEY) ||
            !credentialIsValid(CAIYUN_APP_SECRET))
        {
            return false;
        }
    }
    else if (!credentialIsValid(CAIYUN_TOKEN))
    {
        return false;
    }

    return options.hourlySteps >= 1 &&
           options.hourlySteps <= 360 &&
           options.dailySteps >= 1 &&
           options.dailySteps <= 15 &&
           options.maxAttempts >= 1;
}

bool wifiConfigurationIsValid()
{
    // 开放网络允许空密码，因此这里只校验 SSID 不是空值或示例占位符。
    return !hasPlaceholder(WIFI_SSID);
}

bool connectWifi(Print &log, uint32_t timeoutMs)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }

    log.println(F("[Caiyun] 正在连接 Wi-Fi..."));
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs)
    {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        log.print(F("[Caiyun] Wi-Fi 连接失败，状态="));
        log.println(static_cast<int>(WiFi.status()));
        WiFi.disconnect(false, false);
        return false;
    }

    log.print(F("[Caiyun] Wi-Fi 已连接，RSSI="));
    log.print(WiFi.RSSI());
    log.println(F(" dBm"));
    return true;
}

bool ensureSystemClock(
    Print &log,
    uint32_t timeoutMs,
    bool forceNetworkSync,
    bool &networkSyncCompleted)
{
    networkSyncCompleted = false;
    if (!forceNetworkSync &&
        static_cast<int64_t>(time(nullptr)) >= MinimumValidEpoch)
    {
        return true;
    }

    log.println(forceNetworkSync
                    ? F("[Caiyun] 正在通过 NTP 强制校准系统时间...")
                    : F("[Caiyun] 正在通过 NTP 同步系统时间..."));
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTime(
        0,
        0,
        "ntp.aliyun.com",
        "ntp.tencent.com",
        "pool.ntp.org");

    const uint32_t startedAt = millis();
    while (millis() - startedAt < timeoutMs)
    {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED)
        {
            networkSyncCompleted = true;
        }

        const bool clockAvailable =
            static_cast<int64_t>(time(nullptr)) >= MinimumValidEpoch;
        if (clockAvailable &&
            (!forceNetworkSync || networkSyncCompleted))
        {
            break;
        }
        delay(100);
    }

    const bool available =
        static_cast<int64_t>(time(nullptr)) >= MinimumValidEpoch &&
        (!forceNetworkSync || networkSyncCompleted);
    log.println(available
                    ? F("[Caiyun] 系统时间同步完成")
                    : F("[Caiyun] 系统时间同步超时"));
    return available;
}

void releaseWifi(Print &log)
{
    if (WiFi.getMode() == WIFI_OFF)
    {
        return;
    }

    WiFi.disconnect(true, false);
    log.println(F("[Caiyun] Wi-Fi 已关闭"));
}

void makeNonce(char (&nonce)[33])
{
    static constexpr char HexDigits[] = "0123456789abcdef";
    uint8_t randomBytes[16];
    esp_fill_random(randomBytes, sizeof(randomBytes));

    for (size_t index = 0; index < sizeof(randomBytes); ++index)
    {
        nonce[index * 2] = HexDigits[randomBytes[index] >> 4];
        nonce[index * 2 + 1] = HexDigits[randomBytes[index] & 0x0F];
    }
    nonce[32] = '\0';
}

bool makeSignature(
    const String &path,
    const String &query,
    const char *nonce,
    const char *timestamp,
    String &signature)
{
    String content;
    content.reserve(
        5 + path.length() + query.length() +
        strlen(CAIYUN_APP_KEY) + strlen(nonce) + strlen(timestamp));
    content += F("GET:");
    content += path;
    content += ':';
    content += query;
    content += ':';
    content += CAIYUN_APP_KEY;
    content += ':';
    content += nonce;
    content += ':';
    content += timestamp;

    const mbedtls_md_info_t *sha256 =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (sha256 == nullptr)
    {
        return false;
    }

    uint8_t digest[32];
    const int hmacResult = mbedtls_md_hmac(
        sha256,
        reinterpret_cast<const uint8_t *>(CAIYUN_APP_SECRET),
        strlen(CAIYUN_APP_SECRET),
        reinterpret_cast<const uint8_t *>(content.c_str()),
        content.length(),
        digest);
    if (hmacResult != 0)
    {
        return false;
    }

    uint8_t encoded[48];
    size_t encodedLength = 0;
    const int base64Result = mbedtls_base64_encode(
        encoded,
        sizeof(encoded) - 1,
        &encodedLength,
        digest,
        sizeof(digest));
    if (base64Result != 0)
    {
        return false;
    }

    encoded[encodedLength] = '\0';
    signature = reinterpret_cast<const char *>(encoded);
    signature.replace('+', '-');
    signature.replace('/', '_');
    return true;
}

void createResponseFilter(JsonDocument &filter)
{
    filter["status"] = true;
    filter["server_time"] = true;
    filter["tzshift"] = true;
    filter["error"] = true;
    filter["message"] = true;
    filter["result"]["forecast_keypoint"] = true;

    filter["result"]["realtime"]["status"] = true;
    filter["result"]["realtime"]["temperature"] = true;
    filter["result"]["realtime"]["apparent_temperature"] = true;
    filter["result"]["realtime"]["humidity"] = true;
    filter["result"]["realtime"]["pressure"] = true;
    filter["result"]["realtime"]["visibility"] = true;
    filter["result"]["realtime"]["skycon"] = true;
    filter["result"]["realtime"]["wind"]["speed"] = true;
    filter["result"]["realtime"]["wind"]["direction"] = true;
    filter["result"]["realtime"]["precipitation"]["local"]["intensity"] = true;
    filter["result"]["realtime"]["air_quality"]["aqi"]["chn"] = true;
    filter["result"]["realtime"]["air_quality"]["pm25"] = true;
    filter["result"]["realtime"]["life_index"]["comfort"]["desc"] = true;
    filter["result"]["realtime"]["life_index"]["ultraviolet"]["desc"] = true;

    filter["result"]["hourly"]["status"] = true;
    filter["result"]["hourly"]["description"] = true;
    const char *hourlyValueArrays[] = {
        "temperature",
        "apparent_temperature",
        "humidity",
        "skycon"};
    for (const char *name : hourlyValueArrays)
    {
        filter["result"]["hourly"][name][0]["datetime"] = true;
        filter["result"]["hourly"][name][0]["value"] = true;
    }
    filter["result"]["hourly"]["precipitation"][0]["datetime"] = true;
    filter["result"]["hourly"]["precipitation"][0]["value"] = true;
    filter["result"]["hourly"]["precipitation"][0]["probability"] = true;
    filter["result"]["hourly"]["wind"][0]["datetime"] = true;
    filter["result"]["hourly"]["wind"][0]["speed"] = true;
    filter["result"]["hourly"]["wind"][0]["direction"] = true;

    filter["result"]["daily"]["status"] = true;
    filter["result"]["daily"]["temperature"][0]["date"] = true;
    filter["result"]["daily"]["temperature"][0]["min"] = true;
    filter["result"]["daily"]["temperature"][0]["max"] = true;
    filter["result"]["daily"]["precipitation"][0]["date"] = true;
    filter["result"]["daily"]["precipitation"][0]["probability"] = true;

    const char *dailyValueArrays[] = {
        "skycon",
        "skycon_08h_20h",
        "skycon_20h_32h"};
    for (const char *name : dailyValueArrays)
    {
        filter["result"]["daily"][name][0]["date"] = true;
        filter["result"]["daily"][name][0]["value"] = true;
    }

    filter["result"]["daily"]["astro"][0]["date"] = true;
    filter["result"]["daily"]["astro"][0]["sunrise"]["time"] = true;
    filter["result"]["daily"]["astro"][0]["sunset"]["time"] = true;
    filter["result"]["daily"]["wind"][0]["date"] = true;
    filter["result"]["daily"]["wind"][0]["avg"]["speed"] = true;
    filter["result"]["daily"]["wind"][0]["avg"]["direction"] = true;
    filter["result"]["daily"]["humidity"][0]["date"] = true;
    filter["result"]["daily"]["humidity"][0]["avg"] = true;
    filter["result"]["daily"]["air_quality"]["aqi"][0]["date"] = true;
    filter["result"]["daily"]["air_quality"]["aqi"][0]["avg"]["chn"] = true;
    filter["result"]["daily"]["air_quality"]["pm25"][0]["date"] = true;
    filter["result"]["daily"]["air_quality"]["pm25"][0]["avg"] = true;

    const char *lifeIndexArrays[] = {"ultraviolet", "dressing", "comfort"};
    for (const char *name : lifeIndexArrays)
    {
        filter["result"]["daily"]["life_index"][name][0]["date"] = true;
        filter["result"]["daily"]["life_index"][name][0]["desc"] = true;
    }
}

void assignFloat(JsonVariantConst value, float &target)
{
    if (!value.isNull())
    {
        target = value.as<float>();
    }
}

void assignInt16(JsonVariantConst value, int16_t &target)
{
    if (!value.isNull())
    {
        const int parsed = value.as<int>();
        target = static_cast<int16_t>(constrain(parsed, -32768, 32767));
    }
}

void assignString(JsonVariantConst value, String &target)
{
    if (value.is<const char *>())
    {
        target = value.as<const char *>();
    }
}

HourlyWeatherItem *findHourlyItem(
    std::vector<HourlyWeatherItem> &items,
    const char *datetime)
{
    if (datetime == nullptr || datetime[0] == '\0')
    {
        return nullptr;
    }

    for (HourlyWeatherItem &item : items)
    {
        if (item.datetime == datetime)
        {
            return &item;
        }
    }
    return nullptr;
}

DailyWeatherItem *findDailyItem(
    std::vector<DailyWeatherItem> &items,
    const char *date)
{
    if (date == nullptr || date[0] == '\0')
    {
        return nullptr;
    }

    for (DailyWeatherItem &item : items)
    {
        if (item.date == date)
        {
            return &item;
        }
    }
    return nullptr;
}

void parseRealtime(JsonObjectConst source, RealtimeWeatherState &target)
{
    assignFloat(source["temperature"], target.temperature);
    assignFloat(source["apparent_temperature"], target.apparentTemperature);
    assignFloat(source["humidity"], target.humidity);
    assignFloat(source["pressure"], target.pressure);
    assignFloat(source["visibility"], target.visibility);
    assignFloat(source["precipitation"]["local"]["intensity"], target.precipitationIntensity);
    assignFloat(source["wind"]["speed"], target.wind.speed);
    assignFloat(source["wind"]["direction"], target.wind.direction);
    assignString(source["skycon"], target.skyCondition);
    assignInt16(source["air_quality"]["aqi"]["chn"], target.aqi);
    assignInt16(source["air_quality"]["pm25"], target.pm25);
    assignString(source["life_index"]["comfort"]["desc"], target.comfortDescription);
    assignString(source["life_index"]["ultraviolet"]["desc"], target.ultravioletDescription);
}

void mergeHourlyValueArray(
    JsonArrayConst rows,
    std::vector<HourlyWeatherItem> &items,
    float HourlyWeatherItem::*field)
{
    for (JsonObjectConst row : rows)
    {
        HourlyWeatherItem *item = findHourlyItem(items, row["datetime"]);
        if (item != nullptr)
        {
            assignFloat(row["value"], item->*field);
        }
    }
}

void parseHourly(JsonObjectConst source, HourlyWeatherState &target)
{
    assignString(source["description"], target.description);

    const JsonArrayConst temperatures = source["temperature"].as<JsonArrayConst>();
    target.items.reserve(temperatures.size());
    for (JsonObjectConst row : temperatures)
    {
        const char *datetime = row["datetime"] | "";
        if (datetime[0] == '\0')
        {
            continue;
        }

        HourlyWeatherItem item;
        item.datetime = datetime;
        assignFloat(row["value"], item.temperature);
        target.items.push_back(std::move(item));
    }

    mergeHourlyValueArray(
        source["apparent_temperature"].as<JsonArrayConst>(),
        target.items,
        &HourlyWeatherItem::apparentTemperature);
    mergeHourlyValueArray(
        source["humidity"].as<JsonArrayConst>(),
        target.items,
        &HourlyWeatherItem::humidity);

    for (JsonObjectConst row : source["precipitation"].as<JsonArrayConst>())
    {
        HourlyWeatherItem *item = findHourlyItem(target.items, row["datetime"]);
        if (item != nullptr)
        {
            assignFloat(row["value"], item->precipitation);
            assignInt16(row["probability"], item->precipitationProbability);
        }
    }

    for (JsonObjectConst row : source["wind"].as<JsonArrayConst>())
    {
        HourlyWeatherItem *item = findHourlyItem(target.items, row["datetime"]);
        if (item != nullptr)
        {
            assignFloat(row["speed"], item->wind.speed);
            assignFloat(row["direction"], item->wind.direction);
        }
    }

    for (JsonObjectConst row : source["skycon"].as<JsonArrayConst>())
    {
        HourlyWeatherItem *item = findHourlyItem(target.items, row["datetime"]);
        if (item != nullptr)
        {
            assignString(row["value"], item->skyCondition);
        }
    }
}

void mergeDailyValueArray(
    JsonArrayConst rows,
    std::vector<DailyWeatherItem> &items,
    String DailyWeatherItem::*field)
{
    for (JsonObjectConst row : rows)
    {
        DailyWeatherItem *item = findDailyItem(items, row["date"]);
        if (item != nullptr)
        {
            assignString(row["value"], item->*field);
        }
    }
}

void mergeDailyDescriptionArray(
    JsonArrayConst rows,
    std::vector<DailyWeatherItem> &items,
    String DailyWeatherItem::*field)
{
    for (JsonObjectConst row : rows)
    {
        DailyWeatherItem *item = findDailyItem(items, row["date"]);
        if (item != nullptr)
        {
            assignString(row["desc"], item->*field);
        }
    }
}

void parseDaily(JsonObjectConst source, DailyWeatherState &target)
{
    const JsonArrayConst temperatures = source["temperature"].as<JsonArrayConst>();
    target.items.reserve(temperatures.size());
    for (JsonObjectConst row : temperatures)
    {
        const char *date = row["date"] | "";
        if (date[0] == '\0')
        {
            continue;
        }

        DailyWeatherItem item;
        item.date = date;
        assignFloat(row["min"], item.minimumTemperature);
        assignFloat(row["max"], item.maximumTemperature);
        target.items.push_back(std::move(item));
    }

    for (JsonObjectConst row : source["precipitation"].as<JsonArrayConst>())
    {
        DailyWeatherItem *item = findDailyItem(target.items, row["date"]);
        if (item != nullptr)
        {
            assignInt16(row["probability"], item->precipitationProbability);
        }
    }

    mergeDailyValueArray(
        source["skycon"].as<JsonArrayConst>(),
        target.items,
        &DailyWeatherItem::skyCondition);
    mergeDailyValueArray(
        source["skycon_08h_20h"].as<JsonArrayConst>(),
        target.items,
        &DailyWeatherItem::daytimeSkyCondition);
    mergeDailyValueArray(
        source["skycon_20h_32h"].as<JsonArrayConst>(),
        target.items,
        &DailyWeatherItem::nighttimeSkyCondition);

    for (JsonObjectConst row : source["astro"].as<JsonArrayConst>())
    {
        DailyWeatherItem *item = findDailyItem(target.items, row["date"]);
        if (item != nullptr)
        {
            assignString(row["sunrise"]["time"], item->sunriseTime);
            assignString(row["sunset"]["time"], item->sunsetTime);
        }
    }

    for (JsonObjectConst row : source["wind"].as<JsonArrayConst>())
    {
        DailyWeatherItem *item = findDailyItem(target.items, row["date"]);
        if (item != nullptr)
        {
            assignFloat(row["avg"]["speed"], item->averageWind.speed);
            assignFloat(row["avg"]["direction"], item->averageWind.direction);
        }
    }

    for (JsonObjectConst row : source["humidity"].as<JsonArrayConst>())
    {
        DailyWeatherItem *item = findDailyItem(target.items, row["date"]);
        if (item != nullptr)
        {
            assignFloat(row["avg"], item->averageHumidity);
        }
    }

    for (JsonObjectConst row : source["air_quality"]["aqi"].as<JsonArrayConst>())
    {
        DailyWeatherItem *item = findDailyItem(target.items, row["date"]);
        if (item != nullptr)
        {
            assignInt16(row["avg"]["chn"], item->averageAqi);
        }
    }

    for (JsonObjectConst row : source["air_quality"]["pm25"].as<JsonArrayConst>())
    {
        DailyWeatherItem *item = findDailyItem(target.items, row["date"]);
        if (item != nullptr)
        {
            assignInt16(row["avg"], item->averagePm25);
        }
    }

    const JsonObjectConst lifeIndex = source["life_index"];
    mergeDailyDescriptionArray(
        lifeIndex["ultraviolet"].as<JsonArrayConst>(),
        target.items,
        &DailyWeatherItem::ultravioletDescription);
    mergeDailyDescriptionArray(
        lifeIndex["dressing"].as<JsonArrayConst>(),
        target.items,
        &DailyWeatherItem::dressingDescription);
    mergeDailyDescriptionArray(
        lifeIndex["comfort"].as<JsonArrayConst>(),
        target.items,
        &DailyWeatherItem::comfortDescription);
}

AttemptResult parseResponse(JsonDocument &document, WeatherData &target, Print &log)
{
    const char *status = document["status"] | "";
    if (strcmp(status, "ok") != 0)
    {
        log.print(F("[Caiyun] API 返回失败状态："));
        log.println(status[0] == '\0' ? "<empty>" : status);
        return {WeatherFetchStatus::ApiRejected, HTTP_CODE_OK, 0, false};
    }

    const int64_t serverTime = document["server_time"] | static_cast<int64_t>(0);
    const int32_t tzShift = document["tzshift"] | 0;
    const JsonObjectConst result = document["result"];
    const JsonObjectConst realtime = result["realtime"];
    const JsonObjectConst hourly = result["hourly"];
    const JsonObjectConst daily = result["daily"];

    if (strcmp(realtime["status"] | "", "ok") != 0 ||
        strcmp(hourly["status"] | "", "ok") != 0 ||
        strcmp(daily["status"] | "", "ok") != 0)
    {
        log.println(F("[Caiyun] 响应中的实况/小时/天级子状态不完整"));
        return {WeatherFetchStatus::DataInvalid, HTTP_CODE_OK, 0, true};
    }

    parseRealtime(realtime, target.realtime);
    parseHourly(hourly, target.hourly);
    parseDaily(daily, target.daily);
    assignString(result["forecast_keypoint"], target.forecastKeypoint);

    target.realtime.markAvailable(serverTime, tzShift);
    target.hourly.markAvailable(serverTime, tzShift);
    target.daily.markAvailable(serverTime, tzShift);

    if (!target.isAvailable())
    {
        log.println(F("[Caiyun] 必要天气字段缺失，拒绝替换旧数据"));
        return {WeatherFetchStatus::DataInvalid, HTTP_CODE_OK, 0, true};
    }

    return {WeatherFetchStatus::Success, HTTP_CODE_OK, 0, false};
}

AttemptResult performRequest(
    const CaiyunWeatherClientOptions &options,
    WeatherData &parsed,
    Print &log)
{
    const char *credential = CAIYUN_USE_SIGNED_AUTH
                                 ? CAIYUN_APP_KEY
                                 : CAIYUN_TOKEN;

    String path;
    path.reserve(
        strlen(ApiVersion) + strlen(credential) +
        strlen(LOCATION_LONGITUDE) + strlen(LOCATION_LATITUDE) + 16);
    path += '/';
    path += ApiVersion;
    path += '/';
    path += credential;
    path += '/';
    path += LOCATION_LONGITUDE;
    path += ',';
    path += LOCATION_LATITUDE;
    path += F("/weather");

    // 签名要求 query 按参数名排序，dailysteps 必须位于 hourlysteps 前。
    String query;
    query.reserve(40);
    query += F("dailysteps=");
    query += options.dailySteps;
    query += F("&hourlysteps=");
    query += options.hourlySteps;

    String url;
    url.reserve(path.length() + query.length() + 32);
    url += F("https://");
    url += ApiHost;
    url += path;
    url += '?';
    url += query;

    char nonce[33] = {};
    char timestamp[24] = {};
    String signature;
    if (CAIYUN_USE_SIGNED_AUTH)
    {
        makeNonce(nonce);
        snprintf(
            timestamp,
            sizeof(timestamp),
            "%lld",
            static_cast<long long>(time(nullptr)));
        if (!makeSignature(path, query, nonce, timestamp, signature))
        {
            log.println(F("[Caiyun] 请求签名生成失败"));
            return {WeatherFetchStatus::AuthenticationRejected, 0, 0, false};
        }
    }

    WiFiClientSecure secureClient;
    secureClient.setCACert(CaiyunRootCertificate);
    secureClient.setHandshakeTimeout((options.httpConnectTimeoutMs + 999) / 1000);
    secureClient.setTimeout((options.httpReadTimeoutMs + 999) / 1000);

    HTTPClient http;
    http.setConnectTimeout(options.httpConnectTimeoutMs);
    http.setTimeout(options.httpReadTimeoutMs);
    http.setReuse(false);
    http.useHTTP10(true);
    const char *trackedHeaders[] = {"Retry-After"};
    http.collectHeaders(trackedHeaders, 1);

    if (!http.begin(secureClient, url))
    {
        log.println(F("[Caiyun] 无法初始化 HTTPS 请求"));
        return {WeatherFetchStatus::TlsOrHttpError, 0, 0, true};
    }

    if (CAIYUN_USE_SIGNED_AUTH)
    {
        http.addHeader("x-cy-nonce", nonce);
        http.addHeader("x-cy-timestamp", timestamp);
        http.addHeader("x-cy-signature", signature);
    }

    const uint32_t requestStartedAt = millis();
    const int httpStatus = http.GET();
    const uint32_t requestElapsedMs = millis() - requestStartedAt;
    log.print(F("[Caiyun] HTTP 完成，状态="));
    log.print(httpStatus);
    log.print(F("，耗时="));
    log.print(requestElapsedMs);
    log.println(F(" ms"));
    if (httpStatus < 0)
    {
        log.print(F("[Caiyun] HTTP 客户端错误："));
        log.println(HTTPClient::errorToString(httpStatus));
    }

    if (httpStatus != HTTP_CODE_OK)
    {
        uint32_t retryAfterMs = 0;
        const String retryAfter = http.header("Retry-After");
        if (retryAfter.length() > 0)
        {
            retryAfterMs = static_cast<uint32_t>(retryAfter.toInt()) * 1000U;
        }
        http.end();

        if (httpStatus == HTTP_CODE_UNAUTHORIZED || httpStatus == HTTP_CODE_FORBIDDEN)
        {
            return {WeatherFetchStatus::AuthenticationRejected, httpStatus, 0, false};
        }
        if (httpStatus == 429)
        {
            const bool shortEnoughToRetry = retryAfterMs <= MaximumRetryDelayMs;
            return {
                WeatherFetchStatus::RateLimited,
                httpStatus,
                retryAfterMs,
                shortEnoughToRetry};
        }
        if (httpStatus < 0 ||
            httpStatus == HTTP_CODE_REQUEST_TIMEOUT ||
            httpStatus == 425 ||
            httpStatus >= 500)
        {
            return {WeatherFetchStatus::TlsOrHttpError, httpStatus, 0, true};
        }
        return {WeatherFetchStatus::ApiRejected, httpStatus, 0, false};
    }

    const int responseSize = http.getSize();
    if (responseSize > MaximumResponseBytes)
    {
        log.print(F("[Caiyun] 响应过大，字节数="));
        log.println(responseSize);
        http.end();
        return {WeatherFetchStatus::DataInvalid, httpStatus, 0, false};
    }

    JsonDocument filter;
    createResponseFilter(filter);
    JsonDocument document;
    const DeserializationError jsonError = deserializeJson(
        document,
        http.getStream(),
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(16));
    http.end();

    if (jsonError)
    {
        log.print(F("[Caiyun] JSON 解析失败："));
        log.println(jsonError.c_str());
        return {WeatherFetchStatus::JsonInvalid, httpStatus, 0, true};
    }

    return parseResponse(document, parsed, log);
}

uint32_t retryDelayMs(uint8_t completedAttempt, uint32_t retryAfterMs)
{
    uint32_t window = BaseRetryDelayMs;
    for (uint8_t index = 1; index < completedAttempt; ++index)
    {
        window = min(window * 2U, MaximumRetryDelayMs);
    }

    const uint32_t jitter = esp_random() % (window + 1U);
    return min(max(jitter, retryAfterMs), MaximumRetryDelayMs);
}

void formatApiLocalTime(int64_t serverTime, int32_t tzShiftSeconds, char (&buffer)[24])
{
    const time_t localEpoch = static_cast<time_t>(serverTime + tzShiftSeconds);
    struct tm localTime = {};
    gmtime_r(&localEpoch, &localTime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
}

void printWeatherData(Print &log, const WeatherData &data)
{
    char localTime[24] = {};
    formatApiLocalTime(
        data.realtime.serverTime,
        data.realtime.tzShiftSeconds,
        localTime);

    log.println(F("[Caiyun] ===== 解析后的天气快照 ====="));
    log.print(F("[Caiyun] 地点="));
    log.print(LOCATION_NAME);
    log.print(F("，API当地时间="));
    log.println(localTime);
    log.print(F("[Caiyun] 天气要点="));
    log.println(data.forecastKeypoint);

    const RealtimeWeatherState &realtime = data.realtime;
    log.printf(
        "[Caiyun][实况] %.1f C，体感 %.1f C，湿度 %.0f%%，天气 %s\n",
        realtime.temperature,
        realtime.apparentTemperature,
        realtime.humidity * 100.0F,
        realtime.skyCondition.c_str());
    log.printf(
        "[Caiyun][实况] 降水 %.2f mm/h，风 %.1f km/h @ %.0f deg，气压 %.0f Pa，能见度 %.1f km\n",
        realtime.precipitationIntensity,
        realtime.wind.speed,
        realtime.wind.direction,
        realtime.pressure,
        realtime.visibility);
    log.printf(
        "[Caiyun][实况] AQI %d，PM2.5 %d，舒适度 %s，紫外线 %s\n",
        realtime.aqi,
        realtime.pm25,
        realtime.comfortDescription.c_str(),
        realtime.ultravioletDescription.c_str());

    log.print(F("[Caiyun][小时] 条目数="));
    log.print(data.hourly.items.size());
    log.print(F("，描述="));
    log.println(data.hourly.description);
    for (const HourlyWeatherItem &item : data.hourly.items)
    {
        log.printf(
            "[Caiyun][小时] %s | %.1f C | %s | 降水 %.2f mm/h (%d%%) | 湿度 %.0f%% | 风 %.1f km/h @ %.0f deg\n",
            item.datetime.c_str(),
            item.temperature,
            item.skyCondition.c_str(),
            item.precipitation,
            item.precipitationProbability,
            item.humidity * 100.0F,
            item.wind.speed,
            item.wind.direction);
    }

    log.print(F("[Caiyun][逐日] 条目数="));
    log.println(data.daily.items.size());
    for (const DailyWeatherItem &item : data.daily.items)
    {
        log.printf(
            "[Caiyun][逐日] %s | %.1f~%.1f C | %s/%s | 降水 %d%% | 日出 %s 日落 %s\n",
            item.date.c_str(),
            item.minimumTemperature,
            item.maximumTemperature,
            item.daytimeSkyCondition.c_str(),
            item.nighttimeSkyCondition.c_str(),
            item.precipitationProbability,
            item.sunriseTime.c_str(),
            item.sunsetTime.c_str());
        log.printf(
            "[Caiyun][逐日] 湿度 %.0f%% | 风 %.1f km/h @ %.0f deg | AQI %d | PM2.5 %d | 穿衣 %s | 舒适度 %s | 紫外线 %s\n",
            item.averageHumidity * 100.0F,
            item.averageWind.speed,
            item.averageWind.direction,
            item.averageAqi,
            item.averagePm25,
            item.dressingDescription.c_str(),
            item.comfortDescription.c_str(),
            item.ultravioletDescription.c_str());
    }
    log.println(F("[Caiyun] ===== 天气快照结束 ====="));
}
} // namespace

const char *weatherFetchStatusText(WeatherFetchStatus status)
{
    switch (status)
    {
    case WeatherFetchStatus::Success:
        return "success";
    case WeatherFetchStatus::InvalidConfiguration:
        return "invalid configuration";
    case WeatherFetchStatus::WifiUnavailable:
        return "Wi-Fi unavailable";
    case WeatherFetchStatus::ClockUnavailable:
        return "clock unavailable";
    case WeatherFetchStatus::TlsOrHttpError:
        return "TLS/HTTP error";
    case WeatherFetchStatus::AuthenticationRejected:
        return "authentication rejected";
    case WeatherFetchStatus::RateLimited:
        return "rate limited";
    case WeatherFetchStatus::ApiRejected:
        return "API rejected";
    case WeatherFetchStatus::JsonInvalid:
        return "invalid JSON";
    case WeatherFetchStatus::DataInvalid:
        return "invalid weather data";
    }
    return "unknown";
}

CaiyunWeatherClient::CaiyunWeatherClient(
    Print &logger,
    const CaiyunWeatherClientOptions &options)
    : logger_(logger), options_(options)
{
}

ClockSyncResult CaiyunWeatherClient::synchronizeClock(bool forceNetworkSync)
{
    const uint32_t startedAt = millis();
    ClockSyncResult result;

    if (!wifiConfigurationIsValid())
    {
        logger_.println(F("[Caiyun] Wi-Fi 配置无效，无法同步时间"));
        result.status = ClockSyncStatus::InvalidConfiguration;
        result.elapsedMs = millis() - startedAt;
        return result;
    }

    const bool wifiWasAlreadyConnected = WiFi.status() == WL_CONNECTED;
    if (!connectWifi(logger_, options_.wifiConnectTimeoutMs))
    {
        result.status = ClockSyncStatus::WifiUnavailable;
    }
    else if (ensureSystemClock(
                 logger_,
                 options_.clockSyncTimeoutMs,
                 forceNetworkSync,
                 result.networkSyncCompleted))
    {
        result.status = ClockSyncStatus::Success;
    }
    else
    {
        result.status = ClockSyncStatus::NtpUnavailable;
    }

    if (!wifiWasAlreadyConnected && options_.disconnectWifiAfterRequest)
    {
        releaseWifi(logger_);
    }

    result.elapsedMs = millis() - startedAt;
    return result;
}

WeatherFetchResult CaiyunWeatherClient::refresh(WeatherData &target)
{
    const uint32_t refreshStartedAt = millis();
    WeatherFetchResult result;

    if (!configurationIsValid(options_))
    {
        logger_.println(F("[Caiyun] secrets.h 或请求步数配置无效"));
        result.status = WeatherFetchStatus::InvalidConfiguration;
        result.elapsedMs = millis() - refreshStartedAt;
        return result;
    }

    const bool wifiWasAlreadyConnected = WiFi.status() == WL_CONNECTED;
    AttemptResult lastAttempt;

    for (uint8_t attempt = 1; attempt <= options_.maxAttempts; ++attempt)
    {
        result.attempts = attempt;
        logger_.print(F("[Caiyun] 刷新尝试 "));
        logger_.print(attempt);
        logger_.print('/');
        logger_.println(options_.maxAttempts);

        if (!connectWifi(logger_, options_.wifiConnectTimeoutMs))
        {
            lastAttempt = {
                WeatherFetchStatus::WifiUnavailable,
                0,
                0,
                true};
        }
        else
        {
            bool networkSyncCompleted = false;
            if (!ensureSystemClock(
                    logger_,
                    options_.clockSyncTimeoutMs,
                    false,
                    networkSyncCompleted))
            {
                lastAttempt = {
                    WeatherFetchStatus::ClockUnavailable,
                    0,
                    0,
                    true};
            }
            else
            {
                WeatherData parsed;
                lastAttempt = performRequest(options_, parsed, logger_);
                if (lastAttempt.status == WeatherFetchStatus::Success)
                {
                    target = std::move(parsed);
                    printWeatherData(logger_, target);
                    break;
                }
            }
        }

        logger_.print(F("[Caiyun] 本次尝试失败："));
        logger_.println(weatherFetchStatusText(lastAttempt.status));

        if (!lastAttempt.retryable || attempt == options_.maxAttempts)
        {
            break;
        }

        const uint32_t waitMs = retryDelayMs(attempt, lastAttempt.retryAfterMs);
        logger_.print(F("[Caiyun] 暂时性错误，等待 "));
        logger_.print(waitMs);
        logger_.println(F(" ms 后重试"));
        delay(waitMs);
    }

    if (!wifiWasAlreadyConnected && options_.disconnectWifiAfterRequest)
    {
        releaseWifi(logger_);
    }

    result.status = lastAttempt.status;
    result.httpStatus = lastAttempt.httpStatus;
    result.elapsedMs = millis() - refreshStartedAt;
    logger_.print(F("[Caiyun] 刷新结束："));
    logger_.print(weatherFetchStatusText(result.status));
    logger_.print(F("，总耗时="));
    logger_.print(result.elapsedMs);
    logger_.println(F(" ms"));
    return result;
}
