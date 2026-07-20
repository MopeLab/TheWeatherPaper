#pragma once

#include <Adafruit_GFX.h>

#include "UiDocument.h"

struct UiElementLayoutBounds
{
    int16_t x = 0;
    int16_t y = 0;
    uint16_t width = 1;
    uint16_t height = 1;
    int16_t boxX = 0;
    int16_t boxY = 0;
    uint16_t boxWidth = 1;
    uint16_t boxHeight = 1;
    uint16_t lineCount = 0;
    bool hasLayoutBox = false;
};

// 返回 true 表示调用方已经在当前图层位置完成该元素的自定义绘制，
// 通用渲染器不再执行默认分支。context 由调用方管理生命周期。
using UiElementDrawOverride = bool (*)(
    Adafruit_GFX &target,
    const UiElement &element,
    void *context);

// 同一份文档可绘制到真实 GxEPD2 display 或 1-bit GFXcanvas1。
void drawUiDocument(
    Adafruit_GFX &target,
    const UiDocument &document,
    UiElementDrawOverride drawOverride = nullptr,
    void *drawContext = nullptr);

// 使用设备端的真实字体度量，供浏览器选择框和命中测试复用。
UiElementLayoutBounds measureUiElementLayout(Adafruit_GFX &target, const UiElement &element);
