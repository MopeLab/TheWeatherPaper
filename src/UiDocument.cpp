#include "UiDocument.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace
{
template <typename T>
T clampValue(T value, T minimum, T maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

UiElementType parseElementType(const String &value, bool &valid)
{
    valid = true;
    if (value == "text") return UiElementType::Text;
    if (value == "line") return UiElementType::Line;
    if (value == "rectangle") return UiElementType::Rectangle;
    if (value == "circle") return UiElementType::Circle;
    valid = false;
    return UiElementType::Text;
}

UiTextWrapMode parseTextWrapMode(const String &value, bool &valid)
{
    valid = true;
    if (value == "none") return UiTextWrapMode::None;
    if (value == "smart") return UiTextWrapMode::Smart;
    if (value == "character") return UiTextWrapMode::Character;
    valid = false;
    return UiTextWrapMode::None;
}

UiTextHorizontalAlign parseTextHorizontalAlign(const String &value, bool &valid)
{
    valid = true;
    if (value == "left") return UiTextHorizontalAlign::Left;
    if (value == "center") return UiTextHorizontalAlign::Center;
    if (value == "right") return UiTextHorizontalAlign::Right;
    valid = false;
    return UiTextHorizontalAlign::Left;
}

UiTextVerticalAlign parseTextVerticalAlign(const String &value, bool &valid)
{
    valid = true;
    if (value == "top") return UiTextVerticalAlign::Top;
    if (value == "middle") return UiTextVerticalAlign::Middle;
    if (value == "bottom") return UiTextVerticalAlign::Bottom;
    valid = false;
    return UiTextVerticalAlign::Top;
}

const char *const SupportedFonts[] = {
    "builtin",
    "chinese32",
    "u8g2_font_wqy12_t_gb2312",
    "u8g2_font_wqy14_t_gb2312",
    "u8g2_font_wqy16_t_gb2312",
    "FreeMono9pt7b", "FreeMono12pt7b", "FreeMono18pt7b", "FreeMono24pt7b",
    "FreeMonoBold9pt7b", "FreeMonoBold12pt7b", "FreeMonoBold18pt7b", "FreeMonoBold24pt7b",
    "FreeSans9pt7b", "FreeSans12pt7b", "FreeSans18pt7b", "FreeSans24pt7b",
    "FreeSansBold9pt7b", "FreeSansBold12pt7b", "FreeSansBold18pt7b", "FreeSansBold24pt7b",
    "FreeSerif9pt7b", "FreeSerif12pt7b", "FreeSerif18pt7b", "FreeSerif24pt7b",
    "FreeSerifBold9pt7b", "FreeSerifBold12pt7b", "FreeSerifBold18pt7b", "FreeSerifBold24pt7b",
};

String parseFont(const String &value)
{
    return isSupportedUiFont(value) ? value : "";
}

String limitedString(JsonVariantConst value, const char *fallback, size_t maximumLength)
{
    String result = value.is<const char *>() ? value.as<const char *>() : fallback;
    if (result.length() > maximumLength)
    {
        // 不在 UTF-8 多字节字符中间截断，避免中文或符号变成无效字节序列。
        while (maximumLength > 0 && (static_cast<uint8_t>(result[maximumLength]) & 0xC0) == 0x80)
        {
            --maximumLength;
        }
        result.remove(maximumLength);
    }
    return result;
}

void appendDefaultElement(UiDocument &document, UiElement element)
{
    element.id = document.nextId++;
    document.elements.push_back(element);
}
}

const char *uiElementTypeName(UiElementType type)
{
    switch (type)
    {
    case UiElementType::Text: return "text";
    case UiElementType::Line: return "line";
    case UiElementType::Rectangle: return "rectangle";
    case UiElementType::Circle: return "circle";
    }
    return "text";
}

const char *uiTextWrapModeName(UiTextWrapMode mode)
{
    switch (mode)
    {
    case UiTextWrapMode::None: return "none";
    case UiTextWrapMode::Smart: return "smart";
    case UiTextWrapMode::Character: return "character";
    }
    return "none";
}

const char *uiTextHorizontalAlignName(UiTextHorizontalAlign align)
{
    switch (align)
    {
    case UiTextHorizontalAlign::Left: return "left";
    case UiTextHorizontalAlign::Center: return "center";
    case UiTextHorizontalAlign::Right: return "right";
    }
    return "left";
}

const char *uiTextVerticalAlignName(UiTextVerticalAlign align)
{
    switch (align)
    {
    case UiTextVerticalAlign::Top: return "top";
    case UiTextVerticalAlign::Middle: return "middle";
    case UiTextVerticalAlign::Bottom: return "bottom";
    }
    return "top";
}

bool isSupportedUiFont(const String &font)
{
    for (const char *supported : SupportedFonts)
    {
        if (font == supported)
        {
            return true;
        }
    }
    return false;
}

void resetUiDocument(UiDocument &document)
{
    document.elements.clear();
    document.nextId = 1;

    UiElement frame;
    frame.name = "页面边框";
    frame.type = UiElementType::Rectangle;
    frame.x = 5;
    frame.y = 5;
    frame.width = 390;
    frame.height = 290;
    appendDefaultElement(document, frame);

    UiElement title;
    title.name = "标题";
    title.text = "E-Paper UI Designer";
    title.x = 25;
    title.y = 24;
    title.textSize = 3;
    appendDefaultElement(document, title);

    UiElement divider;
    divider.name = "分割线";
    divider.type = UiElementType::Line;
    divider.x = 20;
    divider.y = 65;
    divider.width = 360;
    divider.height = 0;
    appendDefaultElement(document, divider);

    UiElement hint;
    hint.name = "操作提示";
    hint.text = "Add, drag and edit elements in browser";
    hint.x = 30;
    hint.y = 100;
    hint.textSize = 2;
    appendDefaultElement(document, hint);

    UiElement chinese;
    chinese.name = "中文示例";
    chinese.text = "电子纸设计器";
    chinese.font = "chinese32";
    chinese.x = 95;
    chinese.y = 180;
    appendDefaultElement(document, chinese);
}

String serializeUiDocument(const UiDocument &document)
{
    JsonDocument json;
    json["version"] = 2;
    json["width"] = DesignerWidth;
    json["height"] = DesignerHeight;
    JsonArray elements = json["elements"].to<JsonArray>();

    for (const UiElement &element : document.elements)
    {
        JsonObject item = elements.add<JsonObject>();
        item["id"] = element.id;
        item["name"] = element.name;
        item["type"] = uiElementTypeName(element.type);
        item["visible"] = element.visible;
        item["black"] = element.black;
        item["filled"] = element.filled;
        item["x"] = element.x;
        item["y"] = element.y;
        item["width"] = element.width;
        item["height"] = element.height;
        item["radius"] = element.radius;
        item["strokeWidth"] = element.strokeWidth;
        item["text"] = element.text;
        item["textSize"] = element.textSize;
        item["font"] = element.font;
        item["wrapMode"] = uiTextWrapModeName(element.wrapMode);
        item["horizontalAlign"] = uiTextHorizontalAlignName(element.horizontalAlign);
        item["verticalAlign"] = uiTextVerticalAlignName(element.verticalAlign);
        item["autoHeight"] = element.autoHeight;
        item["lineSpacing"] = element.lineSpacing;
    }

    String result;
    serializeJson(json, result);
    return result;
}

bool deserializeUiDocument(const String &json, UiDocument &document, String &error)
{
    JsonDocument parsed;
    const DeserializationError jsonError = deserializeJson(parsed, json);
    if (jsonError)
    {
        error = String("JSON 解析失败: ") + jsonError.c_str();
        return false;
    }

    const int version = parsed["version"] | 0;
    const int width = parsed["width"] | 0;
    const int height = parsed["height"] | 0;
    if (version != 2)
    {
        error = "仅支持 version 2 的 UI 文档";
        return false;
    }
    if (width != DesignerWidth || height != DesignerHeight)
    {
        error = "UI 文档画布必须为 400 x 300";
        return false;
    }

    JsonArrayConst items = parsed["elements"].as<JsonArrayConst>();
    if (items.isNull())
    {
        error = "缺少 elements 数组";
        return false;
    }
    if (items.size() > MaximumUiElements)
    {
        error = "元素数量超过 64 个";
        return false;
    }

    UiDocument candidate;
    candidate.elements.reserve(items.size());
    uint32_t maximumId = 0;

    for (JsonObjectConst item : items)
    {
        bool validType = false;
        UiElement element;
        element.id = item["id"] | static_cast<uint32_t>(maximumId + 1);
        if (element.id == 0)
        {
            element.id = maximumId + 1;
        }
        element.name = limitedString(item["name"], "未命名元素", 48);
        element.type = parseElementType(limitedString(item["type"], "text", 16), validType);
        if (!validType)
        {
            error = "存在不支持的元素类型";
            return false;
        }
        element.visible = item["visible"] | true;
        element.black = item["black"] | true;
        element.filled = item["filled"] | false;
        element.x = clampValue<int>(item["x"] | 0, -DesignerWidth, DesignerWidth * 2);
        element.y = clampValue<int>(item["y"] | 0, -DesignerHeight, DesignerHeight * 2);
        element.width = clampValue<int>(item["width"] | 80, -DesignerWidth * 2, DesignerWidth * 2);
        element.height = clampValue<int>(item["height"] | 40, -DesignerHeight * 2, DesignerHeight * 2);
        element.radius = clampValue<int>(item["radius"] | 20, 1, DesignerWidth);
        element.strokeWidth = clampValue<int>(item["strokeWidth"] | 1, 1, 8);
        element.text = limitedString(item["text"], "Text", 2048);
        element.textSize = clampValue<int>(item["textSize"] | 2, 1, 8);
        const String requestedFont = limitedString(item["font"], "builtin", 32);
        element.font = parseFont(requestedFont);
        if (element.font.isEmpty())
        {
            error = String("不支持的字体: ") + requestedFont;
            return false;
        }

        bool validWrapMode = false;
        bool validHorizontalAlign = false;
        bool validVerticalAlign = false;
        element.wrapMode = parseTextWrapMode(limitedString(item["wrapMode"], "none", 16), validWrapMode);
        element.horizontalAlign = parseTextHorizontalAlign(limitedString(item["horizontalAlign"], "left", 16), validHorizontalAlign);
        element.verticalAlign = parseTextVerticalAlign(limitedString(item["verticalAlign"], "top", 16), validVerticalAlign);
        element.autoHeight = item["autoHeight"] | true;
        element.lineSpacing = clampValue<int>(item["lineSpacing"] | 0, 0, 32);
        if (!validWrapMode || !validHorizontalAlign || !validVerticalAlign)
        {
            error = "存在不支持的文本布局属性";
            return false;
        }

        for (const UiElement &existing : candidate.elements)
        {
            if (existing.id == element.id)
            {
                element.id = maximumId + 1;
                break;
            }
        }
        maximumId = std::max(maximumId, element.id);
        candidate.elements.push_back(element);
    }

    candidate.nextId = maximumId + 1;
    document = std::move(candidate);
    error = "";
    return true;
}
