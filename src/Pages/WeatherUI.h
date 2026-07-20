#pragma once

#include <Arduino.h>

#include <atomic>

#include "API/CaiyunWeatherClient.h"
#include "DataModel/IndoorEnvironmentState.h"
#include "DataModel/PowerState.h"
#include "DataModel/WeatherData.h"
#include "Sensors/IndoorEnvironmentSensor.h"
#include "UiDocument.h"
#include "Utils.h"

using WeatherDisplay = GxEPD2_BW<EpdDriver, EpdDriver::HEIGHT>;
using WeatherDisplayPrepareCallback = void (*)();

enum class WeatherDisplayOperationEvent : uint8_t
{
    Begin,
    ContentCompleted,
    BlankCompleted
};

using WeatherDisplayOperationCallback =
    void (*)(WeatherDisplayOperationEvent event);

struct WeatherUiRefreshResult
{
    WeatherFetchResult fetch;
    bool layoutReady = false;
    bool weatherUpdated = false;
    bool displayUpdated = false;
    bool displaySkipped = false;
    bool usedCachedWeather = false;
    IndoorEnvironmentReadResult indoorEnvironment;

    // UI 链路成功与天气请求成功是两个概念：请求失败时仍可能把上一份
    // 有效快照或占位内容完整绘制到屏幕。
    bool uiSucceeded() const
    {
        return layoutReady && displayUpdated;
    }
};

// UI 刷新协调层。它把一次刷新固定为：请求天气 -> 判断是否需要呈现
// -> 测量室内环境并更新 DataModel -> 冻结运行时快照 -> 绑定 JSON 布局
// -> 完整刷新电子纸。
class WeatherUI
{
public:
    WeatherUI(
        WeatherDisplay &display,
        CaiyunWeatherClient &weatherClient,
        WeatherData &weatherData,
        Print &logger,
        WeatherDisplayPrepareCallback prepareDisplay = nullptr,
        IndoorEnvironmentReadCallback readIndoorEnvironment = nullptr,
        void *indoorEnvironmentContext = nullptr,
        WeatherDisplayOperationCallback displayOperation = nullptr);

    // 解析编译期嵌入的 epaper-ui-design.json，并校验动态绑定 ID/名称。
    bool begin();

    // 阻塞式完整刷新入口，供普通任务或 loop() 直接调用。
    // 不要从硬件定时器 ISR 中直接调用该方法。
    WeatherUiRefreshResult refresh(bool renderOnFetchFailure = true);

    // 普通软件定时器或事件回调只需登记请求；loop() 中持续调用 process()
    // 即可。硬件 ISR 应先通知普通任务，再由普通任务调用本方法。
    void requestRefresh();
    bool process();

    // 不访问天气 API，仅把当前 DataModel 快照重新呈现到电子纸。
    bool renderCurrent();

    // BOOT 低电平使用的白屏入口。清屏完成后同样立即让电子纸休眠。
    bool clearDisplay();

    void setIndoorEnvironment(const IndoorEnvironmentState &state);
    void setPowerState(const PowerState &state);

    const WeatherData &data() const;
    const String &layoutError() const;
    bool isReady() const;

private:
    bool validateLayout();
    bool buildRuntimeDocument(
        UiDocument &runtime,
        const WeatherData &weather,
        const IndoorEnvironmentState &indoor,
        const PowerState &power);
    bool bindText(
        UiDocument &document,
        uint32_t id,
        const char *expectedName,
        const String &value);
    bool renderSnapshot();

    WeatherDisplay &display_;
    CaiyunWeatherClient &weatherClient_;
    WeatherData &weatherData_;
    Print &logger_;
    WeatherDisplayPrepareCallback prepareDisplay_ = nullptr;
    IndoorEnvironmentReadCallback readIndoorEnvironment_ = nullptr;
    void *indoorEnvironmentContext_ = nullptr;
    WeatherDisplayOperationCallback displayOperation_ = nullptr;
    UiDocument layoutTemplate_;
    IndoorEnvironmentState indoor_;
    PowerState power_;
    String layoutError_;
    std::atomic_bool refreshRequested_{false};
    bool refreshing_ = false;
    bool ready_ = false;
};
