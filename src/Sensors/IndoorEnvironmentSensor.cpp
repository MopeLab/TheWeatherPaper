#include "IndoorEnvironmentSensor.h"

const char *indoorEnvironmentReadStatusText(
    IndoorEnvironmentReadStatus status)
{
    switch (status)
    {
    case IndoorEnvironmentReadStatus::NotAttempted:
        return "未读取";
    case IndoorEnvironmentReadStatus::Success:
        return "成功";
    case IndoorEnvironmentReadStatus::I2cUnavailable:
        return "I2C 初始化失败";
    case IndoorEnvironmentReadStatus::SensorNotFound:
        return "未发现 BME280";
    case IndoorEnvironmentReadStatus::InitializationTimeout:
        return "初始化超时";
    case IndoorEnvironmentReadStatus::MeasurementFailed:
        return "测量失败";
    case IndoorEnvironmentReadStatus::DataInvalid:
        return "数据无效";
    }
    return "未知状态";
}
