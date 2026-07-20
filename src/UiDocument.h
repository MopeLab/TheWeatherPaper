#pragma once

#include <Arduino.h>

#include <vector>

constexpr int16_t DesignerWidth = 400;
constexpr int16_t DesignerHeight = 300;
constexpr size_t MaximumUiElements = 64;

enum class UiElementType : uint8_t
{
    Text,
    Line,
    Rectangle,
    Circle,
};

enum class UiTextWrapMode : uint8_t
{
    None,
    Smart,
    Character,
};

enum class UiTextHorizontalAlign : uint8_t
{
    Left,
    Center,
    Right,
};

enum class UiTextVerticalAlign : uint8_t
{
    Top,
    Middle,
    Bottom,
};

struct UiElement
{
    uint32_t id = 0;
    String name;
    UiElementType type = UiElementType::Text;
    bool visible = true;
    bool black = true;
    bool filled = false;
    int16_t x = 20;
    int16_t y = 20;
    int16_t width = 80;
    int16_t height = 40;
    int16_t radius = 20;
    uint8_t strokeWidth = 1;
    String text = "Text";
    uint8_t textSize = 2;
    String font = "builtin";
    UiTextWrapMode wrapMode = UiTextWrapMode::None;
    UiTextHorizontalAlign horizontalAlign = UiTextHorizontalAlign::Left;
    UiTextVerticalAlign verticalAlign = UiTextVerticalAlign::Top;
    bool autoHeight = true;
    uint8_t lineSpacing = 0;
};

struct UiDocument
{
    std::vector<UiElement> elements;
    uint32_t nextId = 1;
};

const char *uiElementTypeName(UiElementType type);
const char *uiTextWrapModeName(UiTextWrapMode mode);
const char *uiTextHorizontalAlignName(UiTextHorizontalAlign align);
const char *uiTextVerticalAlignName(UiTextVerticalAlign align);
bool isSupportedUiFont(const String &font);
void resetUiDocument(UiDocument &document);
String serializeUiDocument(const UiDocument &document);
bool deserializeUiDocument(const String &json, UiDocument &document, String &error);
