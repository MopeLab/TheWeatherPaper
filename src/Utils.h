#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

using EpdDriver = GxEPD2_420_GDEY042T81;

void initDisplay(GxEPD2_BW<EpdDriver, EpdDriver::HEIGHT> display, int EpdSckPin, int EpdMosiPin, int EpdCsPin);
