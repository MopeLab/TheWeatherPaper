#pragma once

#include <Arduino.h>

#include "DataModel/IndoorEnvironmentState.h"

// 室内环境读取是刷新层的可选数据源。状态单独返回，避免把“传感器
// 读取失败”误当成天气 API 或电子纸刷新失败。
enum class IndoorEnvironmentReadStatus : uint8_t
{
    NotAttempted,
    Success,
    I2cUnavailable,
    SensorNotFound,
    InitializationTimeout,
    MeasurementFailed,
    DataInvalid
};

const char *indoorEnvironmentReadStatusText(
    IndoorEnvironmentReadStatus status);

struct IndoorEnvironmentReadResult
{
    IndoorEnvironmentReadStatus status =
        IndoorEnvironmentReadStatus::NotAttempted;
    IndoorEnvironmentState state;
    uint32_t elapsedMs = 0;

    bool succeeded() const
    {
        return status == IndoorEnvironmentReadStatus::Success &&
               state.isAvailable();
    }
};

// context 由拥有传感器实例的应用层提供。该形式无需动态分配，也便于
// 在主机测试或后续硬件版本中替换传感器实现。
using IndoorEnvironmentReadCallback =
    IndoorEnvironmentReadResult (*)(void *context);
