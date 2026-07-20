#include "Bme280IndoorSensor.h"

#include <math.h>
#include <new>

namespace
{
constexpr uint32_t I2cFrequencyHz = 100000;
constexpr uint16_t I2cTimeoutMs = 50;
constexpr uint32_t CalibrationTimeoutMs = 250;

enum class Bme280InitializationStatus : uint8_t
{
    Success,
    DeviceUnavailable,
    CalibrationTimeout
};

// Adafruit_BME280 2.3.0 的 init() 在等待 NVM 校准复制时没有总超时。
// 这里复用其受保护的寄存器与补偿实现，只把初始化改成有界流程；
// 即使器件在芯片 ID 校验后掉线，也能返回刷新层并继续进入深睡。
class BoundedAdafruitBme280 final : public Adafruit_BME280
{
public:
    Bme280InitializationStatus beginBounded(
        uint8_t address,
        TwoWire &wire,
        uint32_t calibrationTimeoutMs)
    {
        if (i2c_dev != nullptr)
        {
            delete i2c_dev;
            i2c_dev = nullptr;
        }

        i2c_dev = new (std::nothrow) Adafruit_I2CDevice(address, &wire);
        if (i2c_dev == nullptr || !i2c_dev->begin())
        {
            return Bme280InitializationStatus::DeviceUnavailable;
        }

        _sensorID = read8(BME280_REGISTER_CHIPID);
        if (_sensorID != 0x60)
        {
            return Bme280InitializationStatus::DeviceUnavailable;
        }

        write8(BME280_REGISTER_SOFTRESET, 0xB6);
        delay(10);

        const uint32_t calibrationStartedAtMs = millis();
        while (isReadingCalibration())
        {
            if (millis() - calibrationStartedAtMs >= calibrationTimeoutMs)
            {
                return Bme280InitializationStatus::CalibrationTimeout;
            }
            delay(2);
        }

        readCoefficients();
        return Bme280InitializationStatus::Success;
    }
};
}

Bme280IndoorSensor::Bme280IndoorSensor(
    TwoWire &wire,
    int sdaPin,
    int sclPin,
    uint8_t address,
    Print &logger)
    : wire_(wire),
      sdaPin_(sdaPin),
      sclPin_(sclPin),
      address_(address),
      logger_(logger)
{
}

IndoorEnvironmentReadResult Bme280IndoorSensor::read()
{
    IndoorEnvironmentReadResult result;
    const uint32_t startedAtMs = millis();

    auto finish = [&](IndoorEnvironmentReadStatus status) {
        result.status = status;
        result.elapsedMs = millis() - startedAtMs;
        logger_.print(F("[Indoor] BME280 "));
        logger_.print(indoorEnvironmentReadStatusText(status));
        logger_.print(F("，耗时 "));
        logger_.print(result.elapsedMs);
        logger_.println(F(" ms"));
        return result;
    };

    // 必须先显式绑定 GPIO17/GPIO14，再让 Adafruit 驱动创建设备；
    // 否则 Wire 的板级默认 SCL 可能与电子纸 GPIO9/CS 冲突。
    if (!wire_.begin(sdaPin_, sclPin_, I2cFrequencyHz))
    {
        return finish(IndoorEnvironmentReadStatus::I2cUnavailable);
    }
    wire_.setTimeOut(I2cTimeoutMs);

    // 有界初始化会校验 BME280 芯片 ID（0x60），不会只凭地址 ACK
    // 接受 BMP280 或其他 I2C 器件。
    BoundedAdafruitBme280 sensor;
    const Bme280InitializationStatus initialization =
        sensor.beginBounded(address_, wire_, CalibrationTimeoutMs);
    if (initialization == Bme280InitializationStatus::CalibrationTimeout)
    {
        return finish(IndoorEnvironmentReadStatus::InitializationTimeout);
    }
    if (initialization != Bme280InitializationStatus::Success)
    {
        return finish(IndoorEnvironmentReadStatus::SensorNotFound);
    }

    // 低频天气纸每轮只需要一个同步快照。forced mode 完成一次测量后
    // 自动回到 sleep，比 normal mode 持续采样更适合深睡设备。
    sensor.setSampling(
        Adafruit_BME280::MODE_FORCED,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::FILTER_OFF,
        Adafruit_BME280::STANDBY_MS_0_5);
    if (!sensor.takeForcedMeasurement())
    {
        return finish(IndoorEnvironmentReadStatus::MeasurementFailed);
    }

    result.state.update(
        sensor.readTemperature(),
        sensor.readHumidity(),
        sensor.readPressure());
    if (!result.state.isAvailable() || !result.state.hasPressure())
    {
        result.state.invalidate();
        return finish(IndoorEnvironmentReadStatus::DataInvalid);
    }

    logger_.print(F("[Indoor] 温度="));
    logger_.print(result.state.temperatureC, 1);
    logger_.print(F(" °C，湿度="));
    logger_.print(result.state.humidityPercent, 1);
    logger_.print(F(" %，气压="));
    logger_.print(result.state.pressurePa / 100.0f, 1);
    logger_.println(F(" hPa"));
    return finish(IndoorEnvironmentReadStatus::Success);
}
