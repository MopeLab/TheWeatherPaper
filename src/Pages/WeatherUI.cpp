#include "WeatherUI.h"

#include <math.h>
#include <time.h>

#include "Graphics/WeatherIcons.h"
#include "UiRenderer.h"

namespace
{
extern const uint8_t EmbeddedLayoutStart[]
    asm("_binary_assets_fonts_epaper_ui_design_json_start");
extern const uint8_t EmbeddedLayoutEnd[]
    asm("_binary_assets_fonts_epaper_ui_design_json_end");

struct RequiredBinding
{
    uint32_t id;
    const char *name;
    UiElementType type;
};

constexpr RequiredBinding RequiredBindings[] = {
    {2, "日期", UiElementType::Text},
    {4, "天气现象文本", UiElementType::Text},
    {6, "更新时间", UiElementType::Text},
    {7, "天气现象", UiElementType::Rectangle},
    {9, "当前气温", UiElementType::Text},
    {10, "全天温度范围", UiElementType::Text},
    {13, "天气描述", UiElementType::Text},
    {14, "明天天气气象", UiElementType::Rectangle},
    {16, "明天气温范围", UiElementType::Text},
    {17, "室内温度", UiElementType::Text},
    {18, "室内湿度", UiElementType::Text},
    {19, "明天湿度", UiElementType::Text},
    {20, "明天降雨概率", UiElementType::Text},
    {21, "明天天气现象文本", UiElementType::Text},
    {22, "剩余电量", UiElementType::Text},
    {23, "本站气压", UiElementType::Text},
    {25, "后天气温范围", UiElementType::Text},
    {26, "后天天气现象", UiElementType::Rectangle},
    {27, "后天天气现象文本", UiElementType::Text},
    {28, "后天湿度", UiElementType::Text},
    {29, "后天降雨概率", UiElementType::Text},
};

struct WeatherIconSnapshot
{
    String current;
    String tomorrow;
    String dayAfterTomorrow;
};

bool drawWeatherIconElement(
    Adafruit_GFX &target,
    const UiElement &element,
    void *context)
{
    if (context == nullptr || element.type != UiElementType::Rectangle)
    {
        return false;
    }

    const WeatherIconSnapshot &icons =
        *static_cast<const WeatherIconSnapshot *>(context);
    const String *skyCondition = nullptr;
    if (element.id == 7 && element.name == "天气现象")
    {
        skyCondition = &icons.current;
    }
    else if (element.id == 14 && element.name == "明天天气气象")
    {
        skyCondition = &icons.tomorrow;
    }
    else if (element.id == 26 && element.name == "后天天气现象")
    {
        skyCondition = &icons.dayAfterTomorrow;
    }

    if (skyCondition == nullptr)
    {
        return false;
    }

    const uint16_t foreground = element.black ? 0x0000 : 0xFFFF;
    const uint16_t background = element.black ? 0xFFFF : 0x0000;
    drawWeatherIcon(
        target,
        element.x,
        element.y,
        element.width,
        element.height,
        *skyCondition,
        foreground,
        background);
    return true;
}

UiElement *findElementById(UiDocument &document, uint32_t id)
{
    for (UiElement &element : document.elements)
    {
        if (element.id == id)
        {
            return &element;
        }
    }
    return nullptr;
}

String limitUtf8Text(const String &value, size_t maximumLength)
{
    if (value.length() <= maximumLength)
    {
        return value;
    }

    while (maximumLength > 0 &&
           (static_cast<uint8_t>(value[maximumLength]) & 0xC0) == 0x80)
    {
        --maximumLength;
    }
    return value.substring(0, maximumLength);
}

bool localBrokenDownTime(const WeatherData &weather, tm &value)
{
    if (!weather.realtime.isAvailable())
    {
        return false;
    }

    const int64_t shifted = weather.realtime.serverTime +
                            weather.realtime.tzShiftSeconds;
    if (shifted <= 0)
    {
        return false;
    }

    const time_t timestamp = static_cast<time_t>(shifted);
    return gmtime_r(&timestamp, &value) != nullptr;
}

String formatLocalDate(const WeatherData &weather)
{
    tm localTime = {};
    if (!localBrokenDownTime(weather, localTime))
    {
        return "--/--/--";
    }

    char buffer[64];
    snprintf(
        buffer,
        sizeof(buffer),
        "%d/%d/%d",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday);
    return String(buffer);
}

String formatUpdatedTime(const WeatherData &weather)
{
    tm localTime = {};
    if (!localBrokenDownTime(weather, localTime))
    {
        return "已更新:--:--:--";
    }

    char buffer[32];
    snprintf(
        buffer,
        sizeof(buffer),
        "已更新:%02d:%02d:%02d",
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec);
    return String(buffer);
}

String localDateKey(const WeatherData &weather, int dayOffset)
{
    if (!weather.realtime.isAvailable())
    {
        return String();
    }

    const int64_t shifted = weather.realtime.serverTime +
                            weather.realtime.tzShiftSeconds +
                            static_cast<int64_t>(dayOffset) * 24 * 60 * 60;
    const time_t timestamp = static_cast<time_t>(shifted);
    tm localTime = {};
    if (gmtime_r(&timestamp, &localTime) == nullptr)
    {
        return String();
    }

    char buffer[48];
    snprintf(
        buffer,
        sizeof(buffer),
        "%04d-%02d-%02d",
        localTime.tm_year + 1900,
        localTime.tm_mon + 1,
        localTime.tm_mday);
    return String(buffer);
}

const DailyWeatherItem *findDailyItem(
    const WeatherData &weather,
    int dayOffset)
{
    const String key = localDateKey(weather, dayOffset);
    if (key.isEmpty())
    {
        return nullptr;
    }

    for (const DailyWeatherItem &item : weather.daily.items)
    {
        if (item.date.startsWith(key))
        {
            return &item;
        }
    }
    return nullptr;
}

String dailySkyConditionCode(const DailyWeatherItem *item)
{
    if (item == nullptr)
    {
        return String();
    }
    return item->skyCondition.isEmpty()
        ? item->daytimeSkyCondition
        : item->skyCondition;
}

String dailySkyCondition(const DailyWeatherItem *item)
{
    return weatherConditionText(dailySkyConditionCode(item));
}

String formatCurrentTemperature(const WeatherData &weather)
{
    if (!weather.isAvailable() || !isfinite(weather.realtime.temperature))
    {
        return "--℃";
    }
    return String(lroundf(weather.realtime.temperature)) + "℃";
}

String formatTodayTemperatureRange(const DailyWeatherItem *item)
{
    if (item == nullptr ||
        !isfinite(item->minimumTemperature) ||
        !isfinite(item->maximumTemperature))
    {
        return "--°C - --°C";
    }
    return String(lroundf(item->maximumTemperature)) + "°C - " +
           String(lroundf(item->minimumTemperature)) + "°C";
}

String formatFutureTemperatureRange(const DailyWeatherItem *item)
{
    if (item == nullptr ||
        !isfinite(item->minimumTemperature) ||
        !isfinite(item->maximumTemperature))
    {
        return "--°C---°C";
    }
    return String(lroundf(item->minimumTemperature)) + "°C-" +
           String(lroundf(item->maximumTemperature)) + "°C";
}

String formatDailyHumidity(const DailyWeatherItem *item)
{
    if (item == nullptr ||
        !isfinite(item->averageHumidity) ||
        item->averageHumidity < 0.0f ||
        item->averageHumidity > 1.0f)
    {
        return "--%";
    }
    return String(lroundf(item->averageHumidity * 100.0f)) + "%";
}

String formatPrecipitationProbability(const DailyWeatherItem *item)
{
    if (item == nullptr ||
        item->precipitationProbability < 0 ||
        item->precipitationProbability > 100)
    {
        return "降雨概率 --%";
    }
    return String("降雨概率 ") + item->precipitationProbability + "%";
}

String formatPressure(
    const WeatherData &weather,
    const IndoorEnvironmentState &indoor)
{
    if (indoor.hasPressure())
    {
        return String("本站气压 ") +
               lroundf(indoor.pressurePa / 100.0f) +
               " hPa";
    }

    if (!weather.isAvailable() ||
        !isfinite(weather.realtime.pressure) ||
        weather.realtime.pressure <= 0.0f)
    {
        return "本站气压 -- hPa";
    }
    return String("本站气压 ") +
           lroundf(weather.realtime.pressure / 100.0f) +
           " hPa";
}
}

WeatherUI::WeatherUI(
    WeatherDisplay &display,
    CaiyunWeatherClient &weatherClient,
    WeatherData &weatherData,
    Print &logger,
    WeatherDisplayPrepareCallback prepareDisplay,
    IndoorEnvironmentReadCallback readIndoorEnvironment,
    void *indoorEnvironmentContext,
    WeatherDisplayOperationCallback displayOperation)
    : display_(display),
      weatherClient_(weatherClient),
      weatherData_(weatherData),
      logger_(logger),
      prepareDisplay_(prepareDisplay),
      readIndoorEnvironment_(readIndoorEnvironment),
      indoorEnvironmentContext_(indoorEnvironmentContext),
      displayOperation_(displayOperation)
{
}

bool WeatherUI::begin()
{
    ready_ = false;
    layoutError_ = "";

    if (EmbeddedLayoutEnd <= EmbeddedLayoutStart)
    {
        layoutError_ = "嵌入布局资源为空";
    }
    else
    {
        const String json(reinterpret_cast<const char *>(EmbeddedLayoutStart));
        if (!deserializeUiDocument(json, layoutTemplate_, layoutError_))
        {
            // deserializeUiDocument 已写入具体错误。
        }
        else if (!validateLayout())
        {
            // validateLayout 已写入具体错误。
        }
        else
        {
            ready_ = true;
        }
    }

    if (!ready_)
    {
        logger_.print(F("[WeatherUI] 布局初始化失败: "));
        logger_.println(layoutError_);
        return false;
    }

    logger_.print(F("[WeatherUI] 布局已加载，元素数="));
    logger_.println(layoutTemplate_.elements.size());
    return true;
}

WeatherUiRefreshResult WeatherUI::refresh(bool renderOnFetchFailure)
{
    WeatherUiRefreshResult result;
    result.layoutReady = ready_;
    if (!ready_ || refreshing_)
    {
        return result;
    }

    refreshing_ = true;
    const bool hadWeather = weatherData_.isAvailable();
    logger_.println(F("[WeatherUI] 开始天气与屏幕刷新"));

    result.fetch = weatherClient_.refresh(weatherData_);
    result.weatherUpdated = result.fetch.succeeded();
    result.usedCachedWeather = !result.weatherUpdated &&
                               hadWeather &&
                               weatherData_.isAvailable();
    const bool shouldRender = result.weatherUpdated ||
                              (renderOnFetchFailure &&
                               (result.usedCachedWeather || !hadWeather));
    if (shouldRender)
    {
        if (readIndoorEnvironment_ != nullptr)
        {
            result.indoorEnvironment =
                readIndoorEnvironment_(indoorEnvironmentContext_);
            if (result.indoorEnvironment.succeeded())
            {
                indoor_ = result.indoorEnvironment.state;
            }
            else
            {
                // 读取失败时不沿用一份没有陈旧标识的旧室内数据。
                indoor_.invalidate();
            }
        }
        result.displayUpdated = renderSnapshot();
    }
    else
    {
        // 深睡后 RAM 中没有天气快照，但电子纸仍保留上一帧。请求失败时
        // 不应用占位数据覆盖一张仍然有价值的旧画面。
        result.displaySkipped = true;
    }
    refreshing_ = false;

    logger_.print(F("[WeatherUI] 刷新结束，天气="));
    logger_.print(
        result.weatherUpdated
            ? F("新数据")
            : (result.usedCachedWeather
                   ? F("缓存数据")
                   : (result.displaySkipped ? F("无新数据") : F("占位数据"))));
    logger_.print(F("，屏幕="));
    logger_.print(result.displayUpdated
                      ? F("已更新")
                      : (result.displaySkipped ? F("保留原画面") : F("失败")));
    logger_.print(F("，室内环境="));
    logger_.println(indoorEnvironmentReadStatusText(
        result.indoorEnvironment.status));
    return result;
}

void WeatherUI::requestRefresh()
{
    refreshRequested_.store(true, std::memory_order_release);
}

bool WeatherUI::process()
{
    if (refreshing_)
    {
        return false;
    }

    if (!refreshRequested_.exchange(false, std::memory_order_acquire))
    {
        return false;
    }

    refresh();
    return true;
}

bool WeatherUI::renderCurrent()
{
    if (!ready_ || refreshing_)
    {
        return false;
    }

    refreshing_ = true;
    const bool rendered = renderSnapshot();
    refreshing_ = false;
    return rendered;
}

bool WeatherUI::clearDisplay()
{
    if (refreshing_)
    {
        return false;
    }

    refreshing_ = true;
    if (displayOperation_ != nullptr)
    {
        displayOperation_(WeatherDisplayOperationEvent::Begin);
    }
    if (prepareDisplay_ != nullptr)
    {
        prepareDisplay_();
    }
    display_.clearScreen();
    display_.hibernate();
    if (displayOperation_ != nullptr)
    {
        displayOperation_(WeatherDisplayOperationEvent::BlankCompleted);
    }
    refreshing_ = false;
    logger_.println(F("[WeatherUI] BOOT 低电平：电子纸已刷白并休眠"));
    return true;
}

void WeatherUI::setIndoorEnvironment(const IndoorEnvironmentState &state)
{
    indoor_ = state;
}

void WeatherUI::setPowerState(const PowerState &state)
{
    power_ = state;
}

const WeatherData &WeatherUI::data() const
{
    return weatherData_;
}

const String &WeatherUI::layoutError() const
{
    return layoutError_;
}

bool WeatherUI::isReady() const
{
    return ready_;
}

bool WeatherUI::validateLayout()
{
    if (layoutTemplate_.elements.size() != 28)
    {
        layoutError_ = "当前天气布局必须包含 28 个元素";
        return false;
    }

    if (findElementById(layoutTemplate_, 5) != nullptr)
    {
        layoutError_ = "当前天气布局不应包含已删除的 ID 5";
        return false;
    }

    for (const RequiredBinding &binding : RequiredBindings)
    {
        const UiElement *element = findElementById(layoutTemplate_, binding.id);
        if (element == nullptr ||
            element->name != binding.name ||
            element->type != binding.type)
        {
            layoutError_ = String("布局绑定不匹配: ID ") + binding.id +
                           " 应为 " + binding.name;
            return false;
        }
    }
    return true;
}

bool WeatherUI::buildRuntimeDocument(
    UiDocument &runtime,
    const WeatherData &weather,
    const IndoorEnvironmentState &indoor,
    const PowerState &power)
{
    runtime = layoutTemplate_;
    const bool weatherAvailable = weather.isAvailable();
    const DailyWeatherItem *today = weatherAvailable
        ? findDailyItem(weather, 0)
        : nullptr;
    const DailyWeatherItem *tomorrow = weatherAvailable
        ? findDailyItem(weather, 1)
        : nullptr;
    const DailyWeatherItem *dayAfterTomorrow = weatherAvailable
        ? findDailyItem(weather, 2)
        : nullptr;

    String description = "暂无天气数据";
    if (weatherAvailable)
    {
        description = weather.forecastKeypoint.isEmpty()
            ? weather.hourly.description
            : weather.forecastKeypoint;
        if (description.isEmpty())
        {
            description = "暂无天气描述";
        }
    }

    bool valid = true;
    valid &= bindText(runtime, 2, "日期", formatLocalDate(weather));
    valid &= bindText(
        runtime,
        4,
        "天气现象文本",
        weatherAvailable
            ? weatherConditionText(weather.realtime.skyCondition)
            : "未知");
    valid &= bindText(runtime, 6, "更新时间", formatUpdatedTime(weather));
    valid &= bindText(runtime, 9, "当前气温", formatCurrentTemperature(weather));
    valid &= bindText(runtime, 10, "全天温度范围", formatTodayTemperatureRange(today));
    valid &= bindText(runtime, 13, "天气描述", description);
    valid &= bindText(runtime, 16, "明天气温范围", formatFutureTemperatureRange(tomorrow));
    valid &= bindText(
        runtime,
        17,
        "室内温度",
        indoor.isAvailable()
            ? String("室内温度 ") + lroundf(indoor.temperatureC) + "°C"
            : "室内温度 --°C");
    valid &= bindText(
        runtime,
        18,
        "室内湿度",
        indoor.isAvailable()
            ? String("室内湿度 ") + lroundf(indoor.humidityPercent) + " %"
            : "室内湿度 -- %");
    valid &= bindText(runtime, 19, "明天湿度", formatDailyHumidity(tomorrow));
    valid &= bindText(
        runtime,
        20,
        "明天降雨概率",
        formatPrecipitationProbability(tomorrow));
    valid &= bindText(runtime, 21, "明天天气现象文本", dailySkyCondition(tomorrow));
    valid &= bindText(
        runtime,
        22,
        "剩余电量",
        power.isAvailable()
            ? String("剩余电量 ") + lroundf(power.remainingPercent) + "%"
            : "剩余电量 --%");
    valid &= bindText(
        runtime,
        23,
        "本站气压",
        formatPressure(weather, indoor));
    valid &= bindText(
        runtime,
        25,
        "后天气温范围",
        formatFutureTemperatureRange(dayAfterTomorrow));
    valid &= bindText(
        runtime,
        27,
        "后天天气现象文本",
        dailySkyCondition(dayAfterTomorrow));
    valid &= bindText(runtime, 28, "后天湿度", formatDailyHumidity(dayAfterTomorrow));
    valid &= bindText(
        runtime,
        29,
        "后天降雨概率",
        formatPrecipitationProbability(dayAfterTomorrow));
    return valid;
}

bool WeatherUI::bindText(
    UiDocument &document,
    uint32_t id,
    const char *expectedName,
    const String &value)
{
    UiElement *element = findElementById(document, id);
    if (element == nullptr ||
        element->type != UiElementType::Text ||
        element->name != expectedName)
    {
        logger_.print(F("[WeatherUI] 文本绑定失败，ID="));
        logger_.print(id);
        logger_.print(F("，名称="));
        logger_.println(expectedName);
        return false;
    }

    element->text = limitUtf8Text(value, 2048);
    return true;
}

bool WeatherUI::renderSnapshot()
{
    // 在进入 firstPage()/nextPage() 前冻结所有值，确保同一帧的每个
    // page pass 都使用完全相同的数据和字符串。
    const WeatherData weatherSnapshot = weatherData_;
    const IndoorEnvironmentState indoorSnapshot = indoor_;
    const PowerState powerSnapshot = power_;
    UiDocument runtime;
    if (!buildRuntimeDocument(
            runtime,
            weatherSnapshot,
            indoorSnapshot,
            powerSnapshot))
    {
        return false;
    }

    const DailyWeatherItem *tomorrow = weatherSnapshot.isAvailable()
        ? findDailyItem(weatherSnapshot, 1)
        : nullptr;
    const DailyWeatherItem *dayAfterTomorrow = weatherSnapshot.isAvailable()
        ? findDailyItem(weatherSnapshot, 2)
        : nullptr;
    WeatherIconSnapshot icons;
    if (weatherSnapshot.isAvailable())
    {
        icons.current = weatherSnapshot.realtime.skyCondition;
        icons.tomorrow = dailySkyConditionCode(tomorrow);
        icons.dayAfterTomorrow = dailySkyConditionCode(dayAfterTomorrow);
    }

    // API 获取和布局绑定均成功后才唤醒/复位电子纸；请求失败且选择保留
    // 旧画面时完全不触碰面板，确保它继续处于 hibernate 状态。
    if (displayOperation_ != nullptr)
    {
        displayOperation_(WeatherDisplayOperationEvent::Begin);
    }
    if (prepareDisplay_ != nullptr)
    {
        prepareDisplay_();
    }
    display_.setFullWindow();
    display_.firstPage();
    do
    {
        drawUiDocument(
            display_,
            runtime,
            drawWeatherIconElement,
            &icons);
    } while (display_.nextPage());
    display_.hibernate();
    if (displayOperation_ != nullptr)
    {
        displayOperation_(WeatherDisplayOperationEvent::ContentCompleted);
    }
    return true;
}
