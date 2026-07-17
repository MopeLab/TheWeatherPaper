#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "Utils.h"
#include "fonts/pingfang_medium32_gb2312.h"

U8G2_FOR_ADAFRUIT_GFX u8g2;

// 引脚定义
constexpr int EpdCsPin = 9;
constexpr int EpdMosiPin = 13;
constexpr int EpdSckPin = 11;
constexpr int EpdDcPin = 15;
constexpr int EpdRstPin = 8;
constexpr int EpdBusyPin = 6;

constexpr uint8_t BOOT_PIN = 0;
constexpr uint32_t DEBOUNCE_TIME_MS = 40;

// 使用完整 400×300 帧缓冲。
GxEPD2_BW<EpdDriver, EpdDriver::HEIGHT> display(
    EpdDriver(
        EpdCsPin,
        EpdDcPin,
        EpdRstPin,
        EpdBusyPin));
bool lastRawButtonState = HIGH;
bool stableButtonState = HIGH;
unsigned long lastStateChangeTime = 0;

void drawTestPage()
{
    display.fillScreen(GxEPD_WHITE);

    display.setTextColor(GxEPD_BLACK);

    // 外框
    display.drawRect(
        5,
        5,
        display.width() - 10,
        display.height() - 5,
        GxEPD_BLACK);

    // 标题
    display.setTextSize(3);
    display.setCursor(35, 10);
    display.print("The Weather Paper");

    // 副标题
    display.setTextSize(2);
    display.setCursor(35, 60);
    display.print("PROTOTYPE | Ver 0.0.0");

    // 分割线
    display.drawLine(
        20,
        80,
        display.width() - 20,
        80,
        GxEPD_BLACK);

    display.setTextSize(2);

    display.setCursor(35, 110);
    display.print("ESP32-S3");

    display.setCursor(35, 145);
    display.print("Waveshare 4.2inch");

    display.setCursor(35, 175);
    display.print("e-Paper Module V2");

    display.setCursor(35, 215);
    display.print("400 x 300");

    u8g2.setFont(pingfang_medium32_gb2312);
    u8g2.setCursor(250, 150);
    u8g2.print("滚滚长江");
    u8g2.setCursor(266, 200);
    u8g2.print("东逝水");
    // // 简单图形
    // display.drawCircle(320, 150, 40, GxEPD_BLACK);
    // display.drawLine(280, 150, 360, 150, GxEPD_BLACK);
    // display.drawLine(320, 110, 320, 190, GxEPD_BLACK);

    display.setTextSize(1);
    display.setCursor(35, 250);
    display.print("e-Paper Full Refresh Test");

    display.setCursor(35, 270);
    display.print("GxEPD2 SSD1683 Driver");

    display.setCursor(35, 285);
    display.print("Press BOOT button to clear screen");
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    // BOOT 按钮通常按下时将 GPIO0 接到 GND。
    pinMode(BOOT_PIN, INPUT_PULLUP);
    Serial.println();
    Serial.println("Starting e-paper test");

    initDisplay(display, EpdSckPin, EpdMosiPin, EpdCsPin);

    display.setRotation(2);
    u8g2.begin(display);                  // 将 u8g2 绑定到 GxEPD2_BW 对象上
    u8g2.setFontMode(1);                  // 设置字体模式为透明背景
    u8g2.setFontDirection(0);             // 设置字体方向为水平
    u8g2.setForegroundColor(GxEPD_BLACK); // 设置前景色为黑色
    u8g2.setBackgroundColor(GxEPD_WHITE); // 设置背景色为白色
    display.setFullWindow();

    display.firstPage();

    do
    {
        drawTestPage();
    } while (display.nextPage());

    Serial.println("Display refresh complete");

    // 关闭屏幕内部高压驱动，图像仍会保留。
    display.hibernate();

    Serial.println("Display entered hibernate");
}

void loop()
{
    bool rawButtonState = digitalRead(BOOT_PIN);

    // 原始输入发生变化时，重新开始消抖计时。
    if (rawButtonState != lastRawButtonState)
    {
        lastStateChangeTime = millis();
        lastRawButtonState = rawButtonState;
    }

    // 电平稳定足够长时间后，才承认它是真实状态。
    if (millis() - lastStateChangeTime >= DEBOUNCE_TIME_MS)
    {
        if (stableButtonState != rawButtonState)
        {
            stableButtonState = rawButtonState;

            // INPUT_PULLUP 下，LOW 表示按钮被按下。
            if (stableButtonState == LOW)
            {
                display.clearScreen();
                display.hibernate();
            }
        }
    }
}
