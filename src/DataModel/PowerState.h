#pragma once

#include <math.h>

// 电源监测形成的独立值对象，remainingPercent 使用 0-100 百分比。
struct PowerState
{
    bool valid = false;
    float remainingPercent = NAN;

    bool isAvailable() const
    {
        return valid &&
               isfinite(remainingPercent) &&
               remainingPercent >= 0.0f &&
               remainingPercent <= 100.0f;
    }

    void update(float newRemainingPercent)
    {
        remainingPercent = newRemainingPercent;
        valid = isfinite(newRemainingPercent) &&
                newRemainingPercent >= 0.0f &&
                newRemainingPercent <= 100.0f;
    }

    void invalidate()
    {
        valid = false;
    }
};
