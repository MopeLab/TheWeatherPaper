#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <driver/rtc_io.h>
#include <esp_err.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "API/CaiyunWeatherClient.h"
#include "Pages/WeatherUI.h"
#include "Scheduling/RefreshScheduler.h"
#include "Sensors/Bme280IndoorSensor.h"
#include "Utils.h"

namespace
{
// 电子纸引脚。
constexpr int EpdCsPin = 9;
constexpr int EpdMosiPin = 13;
constexpr int EpdSckPin = 11;
constexpr int EpdDcPin = 15;
constexpr int EpdRstPin = 3;
constexpr int EpdBusyPin = 6;

// 板载 BME280：用户给出的顺序按 SDA=GPIO17、SCL=GPIO14 解释。
constexpr int IndoorSensorSdaPin = 14;
constexpr int IndoorSensorSclPin = 17;
constexpr uint8_t IndoorSensorAddress = 0x76;

// ESP32-S3 开发板的 BOOT 通常将 GPIO0 拉低。GPIO0 同时属于 RTC IO，
// 因此可在深睡期间直接作为外部唤醒源。
constexpr uint8_t BootPin = 0;
constexpr gpio_num_t BootRtcPin = GPIO_NUM_0;
constexpr uint32_t DebounceTimeMs = 40;

constexpr int64_t MinimumValidEpoch = 1704067200; // 2024-01-01 UTC
constexpr int64_t ClockResyncIntervalSeconds = 12 * 60 * 60;
constexpr int64_t ClockRetryIntervalSeconds = 10 * 60;
constexpr int64_t ButtonHeldPollSeconds = 5 * 60;
constexpr int64_t MaximumSingleSleepSeconds = 6 * 60 * 60;
constexpr uint32_t RetainedStateMagic = 0x57505452; // "WPTR"
constexpr uint16_t RetainedStateVersion = 1;

enum class ButtonWakePhase : uint8_t
{
    AwaitPress = 0,
    AwaitRelease = 1
};

enum RetainedFlag : uint8_t
{
    RainCadenceFlag = 1U << 0,
    ScreenHasContentFlag = 1U << 1,
    ScreenKnownBlankFlag = 1U << 2,
    DisplayOperationInProgressFlag = 1U << 3
};

// 深睡会重启 CPU 和 C++ 全局对象，因此调度所需的少量状态保存在 RTC
// 慢速内存中。校验和可避免棕断或固件结构变化后误用损坏数据。
struct RetainedSchedulerState
{
    uint32_t magic;
    uint16_t version;
    ButtonWakePhase buttonPhase;
    uint8_t flags;
    int64_t nextRefreshEpoch;
    int64_t nextClockSyncEpoch;
    int64_t displayGuardUntilEpoch;
    int64_t lastFullRefreshEpoch;
    uint32_t checksum;
};

RTC_DATA_ATTR RetainedSchedulerState retainedState;

WeatherData weatherData;

CaiyunWeatherClient &getWeatherClient()
{
    static CaiyunWeatherClient client(Serial);
    return client;
}

// 使用完整 400x300 帧缓冲。深睡后的可靠局刷需要确认电子纸控制器
// previous/current RAM 与供电状态均被保留；无实机证据前统一使用全刷。
WeatherDisplay display(
    EpdDriver(
        EpdCsPin,
        EpdDcPin,
        EpdRstPin,
        EpdBusyPin));

bool displayInitialized = false;
void ensureDisplayInitialized();
int64_t currentEpoch();
void handleDisplayOperation(WeatherDisplayOperationEvent event);

Bme280IndoorSensor &getIndoorSensor()
{
    static Bme280IndoorSensor sensor(
        Wire,
        IndoorSensorSdaPin,
        IndoorSensorSclPin,
        IndoorSensorAddress,
        Serial);
    return sensor;
}

IndoorEnvironmentReadResult readIndoorEnvironment(void *context)
{
    return static_cast<Bme280IndoorSensor *>(context)->read();
}

WeatherUI &getWeatherUi()
{
    static WeatherUI ui(
        display,
        getWeatherClient(),
        weatherData,
        Serial,
        ensureDisplayInitialized,
        readIndoorEnvironment,
        &getIndoorSensor(),
        handleDisplayOperation);
    return ui;
}

bool fallbackTimerActive = false;
uint32_t fallbackDeadlineMs = 0;
bool lastFallbackRawButtonState = HIGH;
bool fallbackStableButtonState = HIGH;
uint32_t fallbackButtonChangedAtMs = 0;

uint32_t calculateRetainedChecksum(const RetainedSchedulerState &state)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&state);
    uint32_t hash = 2166136261UL;
    for (size_t index = 0;
         index < offsetof(RetainedSchedulerState, checksum);
         ++index)
    {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }
    return hash;
}

void saveRetainedState()
{
    retainedState.checksum = calculateRetainedChecksum(retainedState);
}

bool retainedStateIsValid()
{
    constexpr uint8_t KnownFlags =
        static_cast<uint8_t>(RainCadenceFlag) |
        static_cast<uint8_t>(ScreenHasContentFlag) |
        static_cast<uint8_t>(ScreenKnownBlankFlag) |
        static_cast<uint8_t>(DisplayOperationInProgressFlag);
    return retainedState.magic == RetainedStateMagic &&
           retainedState.version == RetainedStateVersion &&
           (retainedState.flags & ~KnownFlags) == 0 &&
           static_cast<uint8_t>(retainedState.buttonPhase) <=
               static_cast<uint8_t>(ButtonWakePhase::AwaitRelease) &&
           retainedState.checksum == calculateRetainedChecksum(retainedState);
}

void resetRetainedState()
{
    memset(&retainedState, 0, sizeof(retainedState));
    retainedState.magic = RetainedStateMagic;
    retainedState.version = RetainedStateVersion;
    retainedState.buttonPhase = ButtonWakePhase::AwaitPress;
    saveRetainedState();
}

bool retainedFlagIsSet(RetainedFlag flag)
{
    return (retainedState.flags & static_cast<uint8_t>(flag)) != 0;
}

void setRetainedFlag(RetainedFlag flag, bool enabled)
{
    if (enabled)
    {
        retainedState.flags |= static_cast<uint8_t>(flag);
    }
    else
    {
        retainedState.flags &= ~static_cast<uint8_t>(flag);
    }
}

void handleDisplayOperation(WeatherDisplayOperationEvent event)
{
    if (event == WeatherDisplayOperationEvent::Begin)
    {
        setRetainedFlag(DisplayOperationInProgressFlag, true);
    }
    else
    {
        // 与“刷屏完成”在同一次 RTC checksum 提交中更新物理屏状态，
        // 避免 hibernate 后、main 后续处理前复位造成元数据落后一帧。
        setRetainedFlag(DisplayOperationInProgressFlag, false);
        const bool contentCompleted =
            event == WeatherDisplayOperationEvent::ContentCompleted;
        setRetainedFlag(ScreenHasContentFlag, contentCompleted);
        setRetainedFlag(ScreenKnownBlankFlag, !contentCompleted);
        retainedState.lastFullRefreshEpoch = currentEpoch();
    }
    saveRetainedState();
}

int64_t currentEpoch()
{
    return static_cast<int64_t>(time(nullptr));
}

bool clockIsValid(int64_t epoch)
{
    return epoch >= MinimumValidEpoch;
}

void ensureDisplayInitialized()
{
    if (displayInitialized)
    {
        return;
    }

    initDisplay(display, EpdSckPin, EpdMosiPin, EpdCsPin);
    display.setRotation(2);
    displayInitialized = true;
}

bool buttonIsPressedAfterDebounce()
{
    if (digitalRead(BootPin) != LOW)
    {
        return false;
    }
    delay(DebounceTimeMs);
    return digitalRead(BootPin) == LOW;
}

bool buttonIsReleasedAfterDebounce()
{
    if (digitalRead(BootPin) != HIGH)
    {
        return false;
    }
    delay(DebounceTimeMs);
    return digitalRead(BootPin) == HIGH;
}

int64_t clampRefreshEpochOutsideQuietHours(int64_t epoch)
{
    if (!clockIsValid(epoch) ||
        !RefreshScheduler::isBeijingQuietHours(epoch))
    {
        return epoch;
    }

    return epoch +
           RefreshScheduler::secondsUntilBeijingQuietEnd(epoch);
}

int64_t secondsUntilNextRetainedAction(int64_t nowEpoch)
{
    if (retainedState.nextRefreshEpoch > nowEpoch)
    {
        return retainedState.nextRefreshEpoch - nowEpoch;
    }
    if (retainedState.displayGuardUntilEpoch > nowEpoch)
    {
        return retainedState.displayGuardUntilEpoch - nowEpoch;
    }
    return 1;
}

void armFallbackTimer(int64_t seconds)
{
    if (seconds < 1)
    {
        seconds = 1;
    }

    // 当前所有策略最长只睡 5 小时；这里仍做饱和保护，避免毫秒换算溢出。
    constexpr int64_t MaximumFallbackSeconds = UINT32_MAX / 1000ULL;
    if (seconds > MaximumFallbackSeconds)
    {
        seconds = MaximumFallbackSeconds;
    }

    fallbackDeadlineMs =
        millis() + static_cast<uint32_t>(seconds * 1000ULL);
    fallbackTimerActive = true;
    lastFallbackRawButtonState = digitalRead(BootPin);
    fallbackStableButtonState =
        retainedState.buttonPhase == ButtonWakePhase::AwaitPress
            ? HIGH
            : LOW;
    fallbackButtonChangedAtMs = millis();

    Serial.print(F("[Power] 深睡不可用，保持唤醒并启用后备定时器，等待 "));
    Serial.print(seconds);
    Serial.println(F(" 秒"));
}

void enterDeepSleepOrFallback(int64_t seconds)
{
    if (seconds < 1)
    {
        seconds = 1;
    }
    if (seconds > MaximumSingleSleepSeconds)
    {
        // 即使绝对时间因 RTC/NTP 跳变异常，也最多睡 6 小时后重新评估，
        // 避免一个损坏的远期时间戳让设备长期失联。
        seconds = MaximumSingleSleepSeconds;
    }

    saveRetainedState();
    fallbackTimerActive = false;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    const esp_err_t timerResult = esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(seconds) * 1000000ULL);
    if (timerResult != ESP_OK)
    {
        Serial.print(F("[Power] 定时唤醒配置失败："));
        Serial.println(esp_err_to_name(timerResult));
        rtc_gpio_deinit(BootRtcPin);
        pinMode(BootPin, INPUT_PULLUP);
        armFallbackTimer(seconds);
        return;
    }

    // ext0 是电平唤醒。按下处理后改为等待 HIGH，防止按钮保持 LOW 时
    // 立即反复唤醒；松手唤醒后再恢复等待 LOW。
    rtc_gpio_init(BootRtcPin);
    rtc_gpio_set_direction(BootRtcPin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(BootRtcPin);
    rtc_gpio_pulldown_dis(BootRtcPin);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    const int wakeLevel =
        retainedState.buttonPhase == ButtonWakePhase::AwaitRelease ? 1 : 0;
    const esp_err_t buttonResult =
        esp_sleep_enable_ext0_wakeup(BootRtcPin, wakeLevel);
    if (buttonResult != ESP_OK)
    {
        // 定时唤醒已成功配置，因此 BOOT 唤醒失败也不会让设备睡死。
        Serial.print(F("[Power] BOOT 唤醒配置失败，仅保留定时唤醒："));
        Serial.println(esp_err_to_name(buttonResult));
    }

    Serial.print(F("[Power] 电子纸已休眠，ESP32-S3 深睡 "));
    Serial.print(seconds);
    Serial.print(F(" 秒，BOOT 等待电平="));
    Serial.println(wakeLevel == 0 ? F("LOW") : F("HIGH"));
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();

    // 正常情况下不会返回；如果底层拒绝进入睡眠，则继续使用后备定时器。
    rtc_gpio_deinit(BootRtcPin);
    pinMode(BootPin, INPUT_PULLUP);
    armFallbackTimer(seconds);
}

void scheduleAfterButtonClear(int64_t nowEpoch)
{
    retainedState.displayGuardUntilEpoch =
        nowEpoch + RefreshScheduler::MinimumRefreshIntervalSeconds;

    int64_t nextRefresh = retainedState.nextRefreshEpoch;
    if (nextRefresh <= nowEpoch)
    {
        nextRefresh = clockIsValid(nowEpoch)
                          ? RefreshScheduler::nextRefreshEpochSeconds(
                                nowEpoch,
                                retainedFlagIsSet(RainCadenceFlag))
                          : nowEpoch + RefreshScheduler::NormalIntervalSeconds;
    }
    if (nextRefresh < retainedState.displayGuardUntilEpoch)
    {
        nextRefresh = retainedState.displayGuardUntilEpoch;
    }
    retainedState.nextRefreshEpoch =
        clampRefreshEpochOutsideQuietHours(nextRefresh);
}

// 返回 true 表示本轮已由 BOOT 状态处理并安排了下一次唤醒。
bool handleBootButtonState()
{
    const int64_t nowEpoch = currentEpoch();

    if (retainedState.buttonPhase == ButtonWakePhase::AwaitRelease)
    {
        if (!buttonIsReleasedAfterDebounce())
        {
            const int64_t guardRemaining =
                retainedState.displayGuardUntilEpoch > nowEpoch
                    ? retainedState.displayGuardUntilEpoch - nowEpoch
                    : ButtonHeldPollSeconds;
            enterDeepSleepOrFallback(guardRemaining);
            return true;
        }

        // 松手唤醒只恢复下一次按下检测，不在同一次唤醒中刷新天气。
        retainedState.buttonPhase = ButtonWakePhase::AwaitPress;
        saveRetainedState();
        enterDeepSleepOrFallback(secondsUntilNextRetainedAction(nowEpoch));
        return true;
    }

    if (!buttonIsPressedAfterDebounce())
    {
        return false;
    }

    bool displayWasCleared = false;
    if (retainedFlagIsSet(ScreenKnownBlankFlag))
    {
        // 已知面板就是白屏时不重复全刷，避免连续短按消耗寿命。
        Serial.println(F("[WeatherUI] BOOT 低电平：屏幕已是白屏，跳过重复全刷"));
    }
    else
    {
        // BOOT 是用户明确要求的即时人工清屏，因此它是 180 秒建议的
        // 唯一例外；清屏后仍会严格阻止下一次 UI 在 180 秒内呈现。
        displayWasCleared = getWeatherUi().clearDisplay();
        if (displayWasCleared)
        {
            scheduleAfterButtonClear(nowEpoch);
        }
    }

    retainedState.buttonPhase = ButtonWakePhase::AwaitRelease;
    saveRetainedState();
    enterDeepSleepOrFallback(
        displayWasCleared
            ? RefreshScheduler::MinimumRefreshIntervalSeconds
            : secondsUntilNextRetainedAction(nowEpoch));
    return true;
}

// 返回 false 表示同步失败且已经安排重试，本轮不能继续天气请求。
bool ensureUsableClock()
{
    int64_t nowEpoch = currentEpoch();
    if (clockIsValid(nowEpoch))
    {
        return true;
    }

    const ClockSyncResult sync = getWeatherClient().synchronizeClock(false);
    nowEpoch = currentEpoch();
    if (!sync.succeeded() || !clockIsValid(nowEpoch))
    {
        Serial.println(F("[Scheduler] 尚无可信时间；不请求天气，10 分钟后重试"));
        retainedState.nextRefreshEpoch = 0;
        retainedState.nextClockSyncEpoch = 0;
        saveRetainedState();
        enterDeepSleepOrFallback(ClockRetryIntervalSeconds);
        return false;
    }

    retainedState.nextClockSyncEpoch =
        nowEpoch + ClockResyncIntervalSeconds;
    saveRetainedState();
    return true;
}

void sleepThroughQuietHours(int64_t nowEpoch)
{
    const int64_t quietRemaining =
        RefreshScheduler::secondsUntilBeijingQuietEnd(nowEpoch);
    retainedState.nextRefreshEpoch = nowEpoch + quietRemaining;
    saveRetainedState();

    Serial.print(F("[Scheduler] 北京时间 01:00--06:00 暂停刷新，距 06:00："));
    Serial.print(quietRemaining);
    Serial.println(F(" 秒"));
    enterDeepSleepOrFallback(quietRemaining);
}

void resynchronizeClockIfDue(int64_t &nowEpoch)
{
    if (retainedState.nextClockSyncEpoch > nowEpoch)
    {
        return;
    }

    const ClockSyncResult sync = getWeatherClient().synchronizeClock(true);
    nowEpoch = currentEpoch();
    retainedState.nextClockSyncEpoch = nowEpoch +
        (sync.succeeded()
             ? ClockResyncIntervalSeconds
             : ClockRetryIntervalSeconds);

    if (sync.succeeded())
    {
        // 强制校时可能把时钟向后拨。若旧的绝对刷新点因此落到一个
        // 常规周期之外，按校准后的当前时间重建它，避免意外久睡。
        const int64_t maximumExpectedDelay =
            RefreshScheduler::refreshIntervalSeconds(
                retainedFlagIsSet(RainCadenceFlag));
        if (retainedState.nextRefreshEpoch >
            nowEpoch + maximumExpectedDelay)
        {
            retainedState.nextRefreshEpoch =
                RefreshScheduler::nextRefreshEpochSeconds(
                    nowEpoch,
                    retainedFlagIsSet(RainCadenceFlag));
        }
    }

    Serial.println(sync.succeeded()
                       ? F("[Scheduler] 12 小时周期 NTP 校准完成")
                       : F("[Scheduler] NTP 校准失败，保留 RTC 时间并稍后重试"));
    saveRetainedState();
}

void runWakeCycle()
{
    if (handleBootButtonState())
    {
        return;
    }

    int64_t nowEpoch = currentEpoch();

    // BOOT 清白属于一次真实全刷；即使松手提前唤醒，也必须等满 180 秒
    // 才允许下一次 UI 呈现。
    if (retainedState.displayGuardUntilEpoch > nowEpoch)
    {
        enterDeepSleepOrFallback(
            retainedState.displayGuardUntilEpoch - nowEpoch);
        return;
    }
    retainedState.displayGuardUntilEpoch = 0;

    if (!ensureUsableClock())
    {
        return;
    }
    nowEpoch = currentEpoch();

    // 有可信时间时，静默判断优先于周期 NTP 和天气联网。冷启动时间未知
    // 时只做一次 clock-only NTP，同步后也会先走这里，不会请求天气。
    if (RefreshScheduler::isBeijingQuietHours(nowEpoch))
    {
        sleepThroughQuietHours(nowEpoch);
        return;
    }

    resynchronizeClockIfDue(nowEpoch);
    if (RefreshScheduler::isBeijingQuietHours(nowEpoch))
    {
        sleepThroughQuietHours(nowEpoch);
        return;
    }

    if (retainedState.nextRefreshEpoch > nowEpoch)
    {
        enterDeepSleepOrFallback(
            retainedState.nextRefreshEpoch - nowEpoch);
        return;
    }

    const bool screenHadContent =
        retainedFlagIsSet(ScreenHasContentFlag);
    WeatherUiRefreshResult result =
        getWeatherUi().refresh(!screenHadContent);

    if (result.weatherUpdated)
    {
        setRetainedFlag(
            RainCadenceFlag,
            RefreshScheduler::weatherRequiresFastCadence(
                getWeatherUi().data()));
    }
    // 请求失败时保留上一次雨天/常规模式；若屏幕已有旧帧，WeatherUI
    // 也会保留原画面，不使用空 DataModel 覆盖它。成功刷新的物理屏
    // 元数据已由显示完成回调与 DisplayOperationInProgressFlag 原子提交。

    const int64_t completedAt = currentEpoch();
    retainedState.displayGuardUntilEpoch =
        completedAt + RefreshScheduler::MinimumRefreshIntervalSeconds;
    retainedState.nextRefreshEpoch =
        RefreshScheduler::nextRefreshEpochSeconds(
            completedAt,
            retainedFlagIsSet(RainCadenceFlag));
    saveRetainedState();

    Serial.print(F("[Scheduler] 下次刷新模式="));
    Serial.print(retainedFlagIsSet(RainCadenceFlag)
                     ? F("雨天 5 分钟")
                     : F("常规 10 分钟"));
    Serial.print(F("，等待 "));
    Serial.print(retainedState.nextRefreshEpoch - completedAt);
    Serial.println(F(" 秒"));

    // renderSnapshot()/clearDisplay() 都已 hibernate；这里再让 ESP32-S3
    // 深睡，下一次 firstPage() 会由 GxEPD2 经硬件 RST 自动唤醒控制器。
    enterDeepSleepOrFallback(
        retainedState.nextRefreshEpoch - completedAt);
}

bool millisDeadlineReached(uint32_t nowMs, uint32_t deadlineMs)
{
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

void pollFallbackButton()
{
    const bool rawState = digitalRead(BootPin);
    const uint32_t nowMs = millis();
    if (rawState != lastFallbackRawButtonState)
    {
        lastFallbackRawButtonState = rawState;
        fallbackButtonChangedAtMs = nowMs;
    }

    if (nowMs - fallbackButtonChangedAtMs < DebounceTimeMs ||
        fallbackStableButtonState == rawState)
    {
        return;
    }

    fallbackStableButtonState = rawState;
    const bool relevantTransition =
        (retainedState.buttonPhase == ButtonWakePhase::AwaitPress &&
         rawState == LOW) ||
        (retainedState.buttonPhase == ButtonWakePhase::AwaitRelease &&
         rawState == HIGH);
    if (relevantTransition)
    {
        fallbackTimerActive = false;
        runWakeCycle();
    }
}
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println(F("Starting The Weather Paper"));

    const esp_sleep_wakeup_cause_t wakeCause =
        esp_sleep_get_wakeup_cause();

    // 深睡前 GPIO0 属于 RTC IO；恢复为普通数字输入后再读取 BOOT。
    rtc_gpio_deinit(BootRtcPin);
    pinMode(BootPin, INPUT_PULLUP);

    const esp_reset_reason_t resetReason = esp_reset_reason();
    const bool powerIntegrityLost =
        resetReason == ESP_RST_POWERON ||
        resetReason == ESP_RST_BROWNOUT ||
        resetReason == ESP_RST_UNKNOWN;

    if (powerIntegrityLost || !retainedStateIsValid())
    {
        resetRetainedState();
        Serial.println(F("[Scheduler] 冷启动：已初始化 RTC 调度状态"));
    }
    else
    {
        Serial.print(F("[Scheduler] 保留 RTC 调度状态，唤醒原因="));
        Serial.println(static_cast<int>(wakeCause));
    }

    // 软件复位/WDT 可能发生在一次刷屏中途。校验有效时保留 RTC 调度，
    // 但把屏幕视为未知并强制重建，避免把半帧当作可用旧画面。
    if (retainedFlagIsSet(DisplayOperationInProgressFlag))
    {
        setRetainedFlag(DisplayOperationInProgressFlag, false);
        setRetainedFlag(ScreenHasContentFlag, false);
        setRetainedFlag(ScreenKnownBlankFlag, false);
        retainedState.nextRefreshEpoch = 0;
        const int64_t recoveredAt = currentEpoch();
        retainedState.displayGuardUntilEpoch =
            recoveredAt + RefreshScheduler::MinimumRefreshIntervalSeconds;
        saveRetainedState();
        Serial.println(F("[Scheduler] 检测到中断的显示流程，将强制重建完整画面"));
    }

    // 布局解析不触碰屏幕；只有真正要清屏或呈现时才初始化电子纸。
    getWeatherUi().begin();
    runWakeCycle();
}

void loop()
{
    // 正常路径不会停留在 loop()。只有深睡配置失败时才在这里保持
    // 可恢复运行，同时继续响应 BOOT 和到期刷新，避免“睡死”。
    if (!fallbackTimerActive)
    {
        delay(20);
        return;
    }

    pollFallbackButton();
    if (fallbackTimerActive &&
        millisDeadlineReached(millis(), fallbackDeadlineMs))
    {
        fallbackTimerActive = false;
        runWakeCycle();
    }
    delay(5);
}
