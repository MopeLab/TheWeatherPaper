#include "UiRenderer.h"

#include <U8g2_for_Adafruit_GFX.h>

#include <algorithm>
#include <climits>
#include <cctype>
#include <vector>

#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMono24pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerif24pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBold18pt7b.h>
#include <Fonts/FreeSerifBold24pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>

#include "fonts/pingfang_medium32_gb2312.h"

namespace
{
constexpr uint16_t Black = 0x0000;
constexpr uint16_t White = 0xFFFF;

const GFXfont *findGfxFont(const String &name)
{
    if (name == "FreeMono9pt7b") return &FreeMono9pt7b;
    if (name == "FreeMono12pt7b") return &FreeMono12pt7b;
    if (name == "FreeMono18pt7b") return &FreeMono18pt7b;
    if (name == "FreeMono24pt7b") return &FreeMono24pt7b;
    if (name == "FreeMonoBold9pt7b") return &FreeMonoBold9pt7b;
    if (name == "FreeMonoBold12pt7b") return &FreeMonoBold12pt7b;
    if (name == "FreeMonoBold18pt7b") return &FreeMonoBold18pt7b;
    if (name == "FreeMonoBold24pt7b") return &FreeMonoBold24pt7b;
    if (name == "FreeSans9pt7b") return &FreeSans9pt7b;
    if (name == "FreeSans12pt7b") return &FreeSans12pt7b;
    if (name == "FreeSans18pt7b") return &FreeSans18pt7b;
    if (name == "FreeSans24pt7b") return &FreeSans24pt7b;
    if (name == "FreeSansBold9pt7b") return &FreeSansBold9pt7b;
    if (name == "FreeSansBold12pt7b") return &FreeSansBold12pt7b;
    if (name == "FreeSansBold18pt7b") return &FreeSansBold18pt7b;
    if (name == "FreeSansBold24pt7b") return &FreeSansBold24pt7b;
    if (name == "FreeSerif9pt7b") return &FreeSerif9pt7b;
    if (name == "FreeSerif12pt7b") return &FreeSerif12pt7b;
    if (name == "FreeSerif18pt7b") return &FreeSerif18pt7b;
    if (name == "FreeSerif24pt7b") return &FreeSerif24pt7b;
    if (name == "FreeSerifBold9pt7b") return &FreeSerifBold9pt7b;
    if (name == "FreeSerifBold12pt7b") return &FreeSerifBold12pt7b;
    if (name == "FreeSerifBold18pt7b") return &FreeSerifBold18pt7b;
    if (name == "FreeSerifBold24pt7b") return &FreeSerifBold24pt7b;
    return nullptr;
}

const uint8_t *findU8g2Font(const String &name)
{
    if (name == "chinese32") return pingfang_medium32_gb2312;
    if (name == "u8g2_font_wqy12_t_gb2312") return u8g2_font_wqy12_t_gb2312;
    if (name == "u8g2_font_wqy14_t_gb2312") return u8g2_font_wqy14_t_gb2312;
    if (name == "u8g2_font_wqy16_t_gb2312") return u8g2_font_wqy16_t_gb2312;
    return nullptr;
}

void drawThickLine(Adafruit_GFX &target, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t width, uint16_t color)
{
    const int16_t start = -static_cast<int16_t>(width - 1) / 2;
    const bool mostlyHorizontal = abs(x2 - x1) >= abs(y2 - y1);
    for (uint8_t index = 0; index < width; ++index)
    {
        const int16_t offset = start + index;
        target.drawLine(
            x1 + (mostlyHorizontal ? 0 : offset),
            y1 + (mostlyHorizontal ? offset : 0),
            x2 + (mostlyHorizontal ? 0 : offset),
            y2 + (mostlyHorizontal ? offset : 0),
            color);
    }
}

void normalizedRectangle(const UiElement &element, int16_t &x, int16_t &y, int16_t &width, int16_t &height)
{
    x = element.width >= 0 ? element.x : element.x + element.width;
    y = element.height >= 0 ? element.y : element.y + element.height;
    width = std::max<int16_t>(1, abs(element.width));
    height = std::max<int16_t>(1, abs(element.height));
}

void drawRectangle(Adafruit_GFX &target, const UiElement &element, uint16_t color)
{
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    normalizedRectangle(element, x, y, width, height);
    if (element.filled)
    {
        target.fillRect(x, y, width, height, color);
        return;
    }

    for (uint8_t index = 0; index < element.strokeWidth && width > index * 2 && height > index * 2; ++index)
    {
        target.drawRect(x + index, y + index, width - index * 2, height - index * 2, color);
    }
}

void drawCircle(Adafruit_GFX &target, const UiElement &element, uint16_t color)
{
    if (element.filled)
    {
        target.fillCircle(element.x, element.y, element.radius, color);
        return;
    }

    for (uint8_t index = 0; index < element.strokeWidth && element.radius > index; ++index)
    {
        target.drawCircle(element.x, element.y, element.radius - index, color);
    }
}

struct TextFontMetrics
{
    bool unicode = false;
    bool builtin = false;
    int16_t ascent = 0;
    int16_t descent = 0;
    int16_t lineHeight = 8;
};

struct TextLineMeasurement
{
    int16_t xOffset = 0;
    int16_t yOffset = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

struct PositionedTextLine
{
    String text;
    int16_t cursorX = 0;
    int16_t cursorY = 0;
    TextLineMeasurement measurement;
};

struct TextLayout
{
    std::vector<PositionedTextLine> lines;
    UiElementLayoutBounds bounds;
};

TextFontMetrics prepareTextFont(Adafruit_GFX &target, U8G2_FOR_ADAFRUIT_GFX &unicodeRenderer, const UiElement &element)
{
    TextFontMetrics metrics;
    if (const uint8_t *u8g2Font = findU8g2Font(element.font))
    {
        metrics.unicode = true;
        unicodeRenderer.setFont(u8g2Font);
        metrics.ascent = std::max<int16_t>(1, unicodeRenderer.getFontAscent());
        metrics.descent = std::min<int16_t>(0, unicodeRenderer.getFontDescent());
        metrics.lineHeight = std::max<int16_t>(1, metrics.ascent - metrics.descent);
        return metrics;
    }

    target.setFont(findGfxFont(element.font));
    target.setTextSize(element.textSize);
    if (element.font == "builtin")
    {
        metrics.builtin = true;
        metrics.lineHeight = 8 * element.textSize;
        metrics.descent = metrics.lineHeight;
        return metrics;
    }

    int16_t x = 0;
    int16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    target.getTextBounds("Ag", 0, 0, &x, &y, &width, &height);
    metrics.ascent = std::max<int16_t>(1, -y);
    metrics.descent = std::max<int16_t>(0, y + static_cast<int16_t>(height));
    metrics.lineHeight = std::max<int16_t>(1, metrics.ascent + metrics.descent);
    return metrics;
}

TextLineMeasurement measureTextLine(
    Adafruit_GFX &target,
    U8G2_FOR_ADAFRUIT_GFX &unicodeRenderer,
    const TextFontMetrics &metrics,
    const String &text)
{
    TextLineMeasurement result;
    result.yOffset = metrics.builtin ? 0 : -metrics.ascent;
    result.height = metrics.lineHeight;
    if (text.isEmpty())
    {
        return result;
    }
    if (metrics.unicode)
    {
        result.width = std::max<int16_t>(0, unicodeRenderer.getUTF8Width(text.c_str()));
        return result;
    }

    int16_t x = 0;
    int16_t y = 0;
    target.getTextBounds(text, 0, 0, &x, &y, &result.width, &result.height);
    result.xOffset = x;
    result.yOffset = y;
    return result;
}

size_t utf8SequenceLength(uint8_t firstByte)
{
    if ((firstByte & 0x80) == 0) return 1;
    if ((firstByte & 0xE0) == 0xC0) return 2;
    if ((firstByte & 0xF0) == 0xE0) return 3;
    if ((firstByte & 0xF8) == 0xF0) return 4;
    return 1;
}

std::vector<String> utf8Characters(const String &text)
{
    std::vector<String> characters;
    for (size_t offset = 0; offset < text.length();)
    {
        size_t length = std::min(utf8SequenceLength(static_cast<uint8_t>(text[offset])), text.length() - offset);
        characters.push_back(text.substring(offset, offset + length));
        offset += length;
    }
    return characters;
}

bool isAsciiWordCharacter(const String &character)
{
    if (character.length() != 1) return false;
    const unsigned char value = static_cast<unsigned char>(character[0]);
    return std::isalnum(value) || value == '_' || value == '\'';
}

bool isWhitespaceCharacter(const String &character)
{
    return character == " " || character == "\t" || character == "　";
}

bool isOpeningPunctuation(const String &character)
{
    const String prohibitedAtLineEnd = "（《【『「〔〈([{“‘";
    return prohibitedAtLineEnd.indexOf(character) >= 0;
}

bool isClosingPunctuation(const String &character)
{
    const String prohibitedAtLineStart = "，。！？；：、）》】』」〕〉…—,.!?;:%)]}”’";
    return prohibitedAtLineStart.indexOf(character) >= 0;
}

std::vector<String> tokenizeText(const String &text, UiTextWrapMode mode)
{
    const std::vector<String> characters = utf8Characters(text);
    std::vector<String> tokens;
    for (size_t index = 0; index < characters.size(); ++index)
    {
        const String &character = characters[index];
        if (character == "\r") continue;
        if (character == "\n")
        {
            tokens.push_back("\n");
            continue;
        }
        if (mode == UiTextWrapMode::Smart && isWhitespaceCharacter(character))
        {
            if (tokens.empty() || tokens.back() != " ") tokens.push_back(" ");
            continue;
        }
        if (mode == UiTextWrapMode::Smart && isAsciiWordCharacter(character))
        {
            String word = character;
            // 限制单次字体度量的长度，避免极端长单词使 U8g2 的 int16_t 宽度溢出。
            while (index + 1 < characters.size() && word.length() < 64 && isAsciiWordCharacter(characters[index + 1]))
            {
                word += characters[++index];
            }
            tokens.push_back(word);
            continue;
        }
        tokens.push_back(character);
    }
    return tokens;
}

void appendTokenByCharacter(
    Adafruit_GFX &target,
    U8G2_FOR_ADAFRUIT_GFX &unicodeRenderer,
    const TextFontMetrics &metrics,
    const String &token,
    uint16_t maximumWidth,
    String &line,
    std::vector<String> &lines)
{
    for (const String &character : utf8Characters(token))
    {
        const String candidate = line + character;
        if (!line.isEmpty() && measureTextLine(target, unicodeRenderer, metrics, candidate).width > maximumWidth)
        {
            lines.push_back(line);
            line = character;
        }
        else
        {
            line = candidate;
        }
    }
}

std::vector<String> wrapText(
    Adafruit_GFX &target,
    U8G2_FOR_ADAFRUIT_GFX &unicodeRenderer,
    const TextFontMetrics &metrics,
    const UiElement &element,
    uint16_t maximumWidth)
{
    const UiTextWrapMode tokenMode = element.wrapMode == UiTextWrapMode::None
        ? UiTextWrapMode::Character
        : element.wrapMode;
    const std::vector<String> tokens = tokenizeText(element.text, tokenMode);
    std::vector<String> lines;
    String line;
    bool pendingSpace = false;

    for (size_t index = 0; index < tokens.size(); ++index)
    {
        String token = tokens[index];
        if (token == "\n")
        {
            lines.push_back(line);
            line = "";
            pendingSpace = false;
            continue;
        }
        if (element.wrapMode == UiTextWrapMode::None)
        {
            line += token;
            continue;
        }
        if (token == " ")
        {
            pendingSpace = !line.isEmpty();
            continue;
        }

        // 中文开括号不能落在行尾，因此与后一个非空白字符或单词作为一组。
        if (element.wrapMode == UiTextWrapMode::Smart && isOpeningPunctuation(token))
        {
            size_t next = index + 1;
            while (next < tokens.size() && tokens[next] == " ") ++next;
            if (next < tokens.size() && tokens[next] != "\n")
            {
                token += tokens[next];
                index = next;
            }
        }

        const String separator = pendingSpace && !line.isEmpty() ? " " : "";
        const String candidate = line + separator + token;
        const uint16_t candidateWidth = measureTextLine(target, unicodeRenderer, metrics, candidate).width;
        if (line.isEmpty() || candidateWidth <= maximumWidth)
        {
            if (line.isEmpty() && candidateWidth > maximumWidth)
            {
                appendTokenByCharacter(target, unicodeRenderer, metrics, token, maximumWidth, line, lines);
            }
            else
            {
                line = candidate;
            }
            pendingSpace = false;
            continue;
        }

        // 逗号、句号、右括号等不能成为新行首：优先把前一个字符一起移到下一行。
        if (element.wrapMode == UiTextWrapMode::Smart && isClosingPunctuation(token))
        {
            std::vector<String> characters = utf8Characters(line);
            if (characters.size() > 1)
            {
                const String last = characters.back();
                String prefix;
                for (size_t characterIndex = 0; characterIndex + 1 < characters.size(); ++characterIndex)
                {
                    prefix += characters[characterIndex];
                }
                if (measureTextLine(target, unicodeRenderer, metrics, last + token).width <= maximumWidth)
                {
                    lines.push_back(prefix);
                    line = last + token;
                    pendingSpace = false;
                    continue;
                }
            }
            line += token;
            lines.push_back(line);
            line = "";
            pendingSpace = false;
            continue;
        }

        lines.push_back(line);
        line = "";
        pendingSpace = false;
        if (measureTextLine(target, unicodeRenderer, metrics, token).width > maximumWidth)
        {
            appendTokenByCharacter(target, unicodeRenderer, metrics, token, maximumWidth, line, lines);
        }
        else
        {
            line = token;
        }
    }

    if (!line.isEmpty() || lines.empty() || (!tokens.empty() && tokens.back() == "\n"))
    {
        lines.push_back(line);
    }
    return lines;
}

TextLayout buildTextLayout(
    Adafruit_GFX &target,
    U8G2_FOR_ADAFRUIT_GFX &unicodeRenderer,
    const UiElement &element)
{
    const TextFontMetrics metrics = prepareTextFont(target, unicodeRenderer, element);
    const bool hasBox = element.wrapMode != UiTextWrapMode::None;
    const uint16_t boxWidth = std::max<int16_t>(1, abs(element.width));
    const int16_t boxLeft = element.width >= 0 ? element.x : element.x + element.width;
    const int16_t boxTop = metrics.builtin ? element.y : element.y - metrics.ascent;
    std::vector<String> lines = wrapText(target, unicodeRenderer, metrics, element, boxWidth);

    const int16_t lineAdvance = metrics.lineHeight + element.lineSpacing;
    const uint32_t completeContentHeight = metrics.lineHeight + (lines.size() - 1) * static_cast<uint32_t>(lineAdvance);
    uint16_t boxHeight = std::min<uint32_t>(UINT16_MAX, completeContentHeight);
    size_t visibleLineCount = lines.size();
    if (hasBox && !element.autoHeight)
    {
        boxHeight = std::max<int16_t>(metrics.lineHeight, abs(element.height));
        visibleLineCount = std::min<size_t>(
            lines.size(),
            std::max<int16_t>(1, (boxHeight + element.lineSpacing) / lineAdvance));
    }

    const uint32_t visibleContentHeight = metrics.lineHeight + (visibleLineCount - 1) * static_cast<uint32_t>(lineAdvance);
    int32_t verticalOffset = 0;
    if (hasBox && boxHeight > visibleContentHeight)
    {
        if (element.verticalAlign == UiTextVerticalAlign::Middle)
        {
            verticalOffset = (boxHeight - visibleContentHeight) / 2;
        }
        else if (element.verticalAlign == UiTextVerticalAlign::Bottom)
        {
            verticalOffset = boxHeight - visibleContentHeight;
        }
    }

    TextLayout layout;
    layout.bounds.hasLayoutBox = hasBox;
    layout.bounds.boxX = boxLeft;
    layout.bounds.boxY = boxTop;
    layout.bounds.boxWidth = hasBox ? boxWidth : 1;
    layout.bounds.boxHeight = hasBox ? boxHeight : visibleContentHeight;
    layout.bounds.lineCount = lines.size();

    int16_t minimumX = INT16_MAX;
    int16_t minimumY = INT16_MAX;
    int16_t maximumX = INT16_MIN;
    int16_t maximumY = INT16_MIN;
    layout.lines.reserve(visibleLineCount);
    for (size_t index = 0; index < visibleLineCount; ++index)
    {
        PositionedTextLine positioned;
        positioned.text = lines[index];
        positioned.measurement = measureTextLine(target, unicodeRenderer, metrics, positioned.text);

        int16_t visualLeft = boxLeft;
        if (hasBox)
        {
            if (element.horizontalAlign == UiTextHorizontalAlign::Center)
            {
                visualLeft += (boxWidth - positioned.measurement.width) / 2;
            }
            else if (element.horizontalAlign == UiTextHorizontalAlign::Right)
            {
                visualLeft += boxWidth - positioned.measurement.width;
            }
        }
        else if (element.horizontalAlign == UiTextHorizontalAlign::Center)
        {
            visualLeft -= positioned.measurement.width / 2;
        }
        else if (element.horizontalAlign == UiTextHorizontalAlign::Right)
        {
            visualLeft -= positioned.measurement.width;
        }

        positioned.cursorX = visualLeft - positioned.measurement.xOffset;
        const int32_t lineTop = static_cast<int32_t>(boxTop) + verticalOffset + index * static_cast<int32_t>(lineAdvance);
        // 画布外的超长文本仍参与总行数和自动高度计算，但不创建会溢出 int16_t 的绘制坐标。
        if (lineTop >= target.height() || lineTop + metrics.lineHeight <= 0)
        {
            continue;
        }
        positioned.cursorY = lineTop + (metrics.builtin ? 0 : metrics.ascent);
        if (!positioned.text.isEmpty())
        {
            const int16_t inkX = positioned.cursorX + positioned.measurement.xOffset;
            const int16_t inkY = positioned.cursorY + positioned.measurement.yOffset;
            minimumX = std::min(minimumX, inkX);
            minimumY = std::min(minimumY, inkY);
            maximumX = std::max<int16_t>(maximumX, inkX + positioned.measurement.width);
            maximumY = std::max<int16_t>(maximumY, inkY + positioned.measurement.height);
        }
        layout.lines.push_back(std::move(positioned));
    }

    if (minimumX == INT16_MAX)
    {
        minimumX = boxLeft;
        minimumY = boxTop + verticalOffset;
        maximumX = minimumX + 1;
        maximumY = minimumY + metrics.lineHeight;
    }
    layout.bounds.x = minimumX;
    layout.bounds.y = minimumY;
    layout.bounds.width = std::max<int16_t>(1, maximumX - minimumX);
    layout.bounds.height = std::max<int16_t>(1, maximumY - minimumY);
    if (!hasBox)
    {
        layout.bounds.boxX = layout.bounds.x;
        layout.bounds.boxY = layout.bounds.y;
        layout.bounds.boxWidth = layout.bounds.width;
        layout.bounds.boxHeight = layout.bounds.height;
    }
    return layout;
}

void restoreDefaultTextState(Adafruit_GFX &target)
{
    target.setFont(nullptr);
    target.setTextSize(1);
}
}

void drawUiDocument(
    Adafruit_GFX &target,
    const UiDocument &document,
    UiElementDrawOverride drawOverride,
    void *drawContext)
{
    target.fillScreen(White);
    target.setTextWrap(false);

    U8G2_FOR_ADAFRUIT_GFX unicodeRenderer;
    unicodeRenderer.begin(target);
    unicodeRenderer.setFontMode(1);
    unicodeRenderer.setFontDirection(0);
    unicodeRenderer.setForegroundColor(Black);
    unicodeRenderer.setBackgroundColor(White);
    unicodeRenderer.setFont(pingfang_medium32_gb2312);

    for (const UiElement &element : document.elements)
    {
        if (!element.visible)
        {
            continue;
        }

        if (drawOverride != nullptr &&
            drawOverride(target, element, drawContext))
        {
            continue;
        }

        const uint16_t color = element.black ? Black : White;
        switch (element.type)
        {
        case UiElementType::Text:
        {
            const bool usesUnicodeFont = findU8g2Font(element.font) != nullptr;
            unicodeRenderer.setForegroundColor(color);
            target.setTextColor(color);
            TextLayout layout = buildTextLayout(target, unicodeRenderer, element);
            for (const PositionedTextLine &line : layout.lines)
            {
                if (line.text.isEmpty()) continue;
                if (usesUnicodeFont)
                {
                    unicodeRenderer.drawUTF8(line.cursorX, line.cursorY, line.text.c_str());
                }
                else
                {
                    target.setCursor(line.cursorX, line.cursorY);
                    target.print(line.text);
                }
            }
            restoreDefaultTextState(target);
            break;
        }
        case UiElementType::Line:
            drawThickLine(
                target,
                element.x,
                element.y,
                element.x + element.width,
                element.y + element.height,
                element.strokeWidth,
                color);
            break;
        case UiElementType::Rectangle:
            drawRectangle(target, element, color);
            break;
        case UiElementType::Circle:
            drawCircle(target, element, color);
            break;
        }
    }
}

UiElementLayoutBounds measureUiElementLayout(Adafruit_GFX &target, const UiElement &element)
{
    if (element.type == UiElementType::Circle)
    {
        UiElementLayoutBounds result;
        result.x = element.x - element.radius;
        result.y = element.y - element.radius;
        result.width = element.radius * 2 + 1;
        result.height = element.radius * 2 + 1;
        result.boxX = result.x;
        result.boxY = result.y;
        result.boxWidth = result.width;
        result.boxHeight = result.height;
        return result;
    }
    if (element.type != UiElementType::Text)
    {
        UiElementLayoutBounds result;
        int16_t x;
        int16_t y;
        int16_t width;
        int16_t height;
        normalizedRectangle(element, x, y, width, height);
        result.x = x;
        result.y = y;
        result.width = width;
        result.height = height;
        result.boxX = x;
        result.boxY = y;
        result.boxWidth = width;
        result.boxHeight = height;
        return result;
    }

    U8G2_FOR_ADAFRUIT_GFX unicodeRenderer;
    unicodeRenderer.begin(target);
    unicodeRenderer.setFontMode(1);
    unicodeRenderer.setFontDirection(0);
    unicodeRenderer.setForegroundColor(Black);
    unicodeRenderer.setBackgroundColor(White);
    TextLayout layout = buildTextLayout(target, unicodeRenderer, element);
    restoreDefaultTextState(target);
    return layout.bounds;
}
