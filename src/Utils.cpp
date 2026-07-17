#include "Utils.h"


void initDisplay(GxEPD2_BW<EpdDriver, EpdDriver::HEIGHT> display, int EpdSckPin, int EpdMosiPin, int EpdCsPin)
{
    // 墨水屏没有 MISO，因此该位置使用 -1。
    SPI.begin(
        EpdSckPin,
        -1,
        EpdMosiPin,
        EpdCsPin);

    // 初次实验使用较保守的 4 MHz、SPI Mode 0。
    display.epd2.selectSPI(
        SPI,
        SPISettings(
            4000000,
            MSBFIRST,
            SPI_MODE0));

    // reset_duration = 2 ms 对部分 Waveshare 新版复位电路更兼容。
    display.init(
        115200,
        true,
        2,
        false);
}
