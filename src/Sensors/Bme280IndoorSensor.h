#pragma once

#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <Wire.h>

#include "IndoorEnvironmentSensor.h"

// BME280 单次测量器。每次 read() 都显式初始化指定 I2C 引脚和地址，
// 以适配 ESP32-S3 深睡唤醒后从头执行 setup() 的运行方式。
class Bme280IndoorSensor
{
public:
    Bme280IndoorSensor(
        TwoWire &wire,
        int sdaPin,
        int sclPin,
        uint8_t address,
        Print &logger);

    IndoorEnvironmentReadResult read();

private:
    TwoWire &wire_;
    int sdaPin_;
    int sclPin_;
    uint8_t address_;
    Print &logger_;
};
