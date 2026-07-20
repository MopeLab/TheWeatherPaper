# `ui-design.json` 复现与 DataModel 绑定规范

本文档是 `assets/epaper-ui-design.json` 的实现契约，目标是让另一个 Codex 任务在相同 ESP32-S3 与 4.2 英寸黑白电子纸硬件上，完整复现本设计器的 400×300、1-bit 预览及文本排版行为。

文中的“必须”表示像素级复现所需条件。不要把 JSON 当成普通绝对定位文本列表后自行估算字符宽度；中文、ASCII、`℃`、`°`、标点混排必须使用设备端真实字体度量。

> **数据警告：** 示例 JSON 中的日期、时间、天气、温湿度、气压、电量和概率均是预览用随机值，不是业务默认值。正式项目必须按第 7 节从 DataModel 生成运行时字符串。坐标、尺寸、字体、对齐、换行、图层顺序等布局属性才是需要原样复用的设计数据。

## 1. 权威来源与最短实现路径

设计文件：

- `assets/epaper-ui-design.json`
- 当前版本为 `version: 2`
- 画布固定为 `400 × 300`
- 当前示例有 28 个元素，ID 为 `1,2,3,4,6...29`；缺少 ID 5 是正常的，不得按数组下标重编号

像素级权威实现：

- JSON 模型和校验：`src/UiDocument.h`、`src/UiDocument.cpp`
- 图元、字体度量、换行和对齐：`src/UiRenderer.h`、`src/UiRenderer.cpp`
- 32px 中文字体：`include/fonts/pingfang_medium32_gb2312.h`
- 硬件方向和刷新方式：`src/main.cpp`、`src/Utils.h`、`src/Utils.cpp`

最可靠的移植方式是直接复用上述 `UiDocument` 和 `UiRenderer`，不要在目标项目重新写一套近似排版器。若必须重写，则本文件后续规则全部属于必需行为。

目标项目至少需要以下库。若要求逐像素一致，应锁定设计器编译验证时实际解析到的版本：

```ini
lib_deps =
    zinggjm/GxEPD2@1.6.9
    adafruit/Adafruit GFX Library@1.12.6
    olikraus/U8g2_for_Adafruit_GFX@1.8.0
    bblanchon/ArduinoJson@7.4.3
```

## 2. 硬件与画布契约

必须使用以下显示配置：

```cpp
using EpdDriver = GxEPD2_420_GDEY042T81;

constexpr int EpdCsPin   = 9;
constexpr int EpdMosiPin = 13;
constexpr int EpdSckPin  = 11;
constexpr int EpdDcPin   = 15;
constexpr int EpdRstPin  = 8;
constexpr int EpdBusyPin = 6;
```

- 逻辑画布：400×300，原点 `(0, 0)` 位于左上角，X 向右、Y 向下。
- 显示驱动：`GxEPD2_420_GDEY042T81`。
- 初始化完成后必须调用 `display.setRotation(2)`。
- SPI 使用硬件 SPI，当前设计器采用 4 MHz、MSB first、SPI mode 0。
- 预览和实体屏必须调用同一个 `drawUiDocument()`；不得分别维护浏览器版和设备版排版逻辑。
- 每帧先用白色清屏，再按 JSON `elements` 的数组顺序从前到后绘制。
- 实体屏使用 `setFullWindow()` 和 `firstPage()/nextPage()` 完整刷新；每个 page pass 都调用同一个 `drawUiDocument()`。
- 渲染结束可调用 `display.hibernate()`，这不会清除电子纸图像。

设计器中的蓝色真实边界框、橙色文本布局框和网格只属于编辑器覆盖层，不属于电子纸输出。

## 3. 文档结构

```json
{
  "version": 2,
  "width": 400,
  "height": 300,
  "elements": []
}
```

目标实现必须拒绝或明确迁移非 400×300 的文档。当前固件最多接收 64 个元素。

每个元素均序列化完整字段，即使某些字段对当前 `type` 无效：

```json
{
  "id": 13,
  "name": "天气描述",
  "type": "text",
  "visible": true,
  "black": true,
  "filled": false,
  "x": 75,
  "y": 205,
  "width": 250,
  "height": 50,
  "radius": 25,
  "strokeWidth": 1,
  "text": "小雨，今天下午15点钟后转中雨，其后阴",
  "textSize": 2,
  "font": "u8g2_font_wqy12_t_gb2312",
  "wrapMode": "smart",
  "horizontalAlign": "center",
  "verticalAlign": "top",
  "autoHeight": true,
  "lineSpacing": 0
}
```

### 3.1 公共字段

| 字段 | 含义与约束 |
|---|---|
| `id` | 图层稳定标识。绑定动态数据时优先按 ID 查找，并同时检查 `name`；不要按数组下标绑定。 |
| `name` | 人类可读名称，不参与绘制。当前固件最多保留 48 UTF-8 字节。 |
| `type` | 仅允许 `text`、`line`、`rectangle`、`circle`。 |
| `visible` | `false` 时完全跳过该元素。 |
| `black` | `true` 使用黑色，`false` 使用白色。白色元素会擦除此前已绘制的黑色像素，因此图层顺序仍然重要。 |
| `x`,`y` | 坐标；文本的精确语义见第 5 节。 |
| `width`,`height` | 线段终点增量、矩形尺寸或文本框尺寸，具体取决于类型。 |
| `filled` | 仅用于矩形和圆形；`true` 为填充。 |
| `radius` | 仅用于圆形。当前范围 1–400。 |
| `strokeWidth` | 线、空心矩形、空心圆的线宽，范围 1–8。 |

当前反序列化范围为：X `-400..800`、Y `-300..600`、width `-800..800`、height `-600..600`、`textSize` `1..8`、`lineSpacing` `0..32`。文本最多保留 2048 UTF-8 字节，截断时不得截在多字节字符中间。

### 3.2 无关字段处理

- `line`、`rectangle`、`circle` 中出现的 `text`、`font`、`wrapMode` 等字段不参与绘制。
- `text` 中的 `filled`、`radius`、`strokeWidth` 不参与绘制。
- 不要因为无关字段带有默认值而额外画出内容。

## 4. 图层顺序和非文本图元

### 4.1 图层顺序

`elements[0]` 最先绘制，最后一个元素最后绘制。网页图层面板为了操作方便会反向显示，但这不改变 JSON 的绘制顺序。

### 4.2 直线

- 起点：`(x, y)`。
- 终点：`(x + width, y + height)`。
- `width`、`height` 是增量，不是边界框尺寸。
- 粗线由多条平行线组成：若 `abs(dx) >= abs(dy)`，在线的 Y 方向增加偏移；否则在 X 方向增加偏移。
- 偏移起点为 `-(strokeWidth - 1) / 2` 的整数结果，并连续绘制 `strokeWidth` 条线。

### 4.3 矩形

- 允许负 `width` 或负 `height`。
- 规范化左上角：负宽时使用 `x + width`，负高时使用 `y + height`。
- 实际宽高为 `max(1, abs(value))`。
- 填充矩形调用 `fillRect()`。
- 空心粗边框从外向内循环调用 `drawRect(x+i, y+i, width-2i, height-2i)`。

### 4.4 圆形

- `(x, y)` 是圆心，不是左上角。
- 填充圆使用 `fillCircle(x, y, radius)`。
- 空心粗圆从半径 `radius` 向内绘制多个同心圆。

## 5. 文本字体、坐标和真实边界

绘制整帧前必须调用 `target.setTextWrap(false)`，因为本格式使用自己的换行算法，不能启用 Adafruit_GFX 自动换行。

U8g2 桥接器必须使用透明字体背景：`setFontMode(1)`、方向 `setFontDirection(0)`，前景色随元素的 `black` 切换，背景色设为白色。不要让每个 glyph 自动填充不透明背景矩形。

### 5.1 本示例实际使用的字体

| JSON `font` | 渲染器 | 坐标 Y 语义 | `textSize` |
|---|---|---|---|
| `builtin` | Adafruit_GFX 内置 6×8 点阵字体 | 字符单元顶部 | 生效，按整数倍缩放 |
| `chinese32` | `pingfang_medium32_gb2312` + U8g2 | 字体基线 | 忽略 |
| `u8g2_font_wqy12_t_gb2312` | U8g2 | 字体基线 | 忽略 |
| `u8g2_font_wqy14_t_gb2312` | U8g2 | 字体基线 | 忽略 |
| `u8g2_font_wqy16_t_gb2312` | U8g2 | 字体基线 | 忽略 |

`chinese32` 不是运行时加载的 OTF 字体，必须把 `include/fonts/pingfang_medium32_gb2312.h` 编译进目标固件。`assets/fonts/PingFangSC-Medium.otf`、`gb2312.txt` 和 `gb2312.map` 是生成源与映射资料，不是运行时依赖。

当前设计器还接受 Adafruit FreeMono/FreeSans/FreeSerif 的 9、12、18、24pt 普通及 Bold 字体。如果未来 JSON 使用它们，必须沿用 `src/UiDocument.cpp` 中 `SupportedFonts` 和 `src/UiRenderer.cpp` 中 `findGfxFont()` 的完整映射。

字体只会绘制其字库包含的字符。不得用系统字体、浏览器 Canvas、TTF 渲染或等宽字符估算替代上述嵌入字体。

### 5.2 真实字体度量

- U8g2 字体的行宽必须使用 `U8G2_FOR_ADAFRUIT_GFX::getUTF8Width()`。
- Adafruit_GFX 内置/FreeFonts 必须使用 `Adafruit_GFX::getTextBounds()`。
- FreeFonts 的 glyph X offset 必须从最终 cursor X 中扣除，否则左、中、右对齐会偏移。
- U8g2 行高为 `fontAscent - fontDescent`；字体 descent 按 U8g2 的负值语义处理。
- 内置字体行高为 `8 * textSize`。
- 不得使用“字符数 × 字号”、中文一律全角宽或浏览器 `measureText()` 作为最终结果。

设计器的 `/api/layout` 和选框也使用同一设备端度量。目标项目若不需要编辑器 API，可以不实现该路由，但绘制算法必须保持一致。

## 6. 换行和对齐算法

### 6.1 `wrapMode`

`none`：

- 不按 `width` 自动换行。
- 只识别文本中的显式 `\n`；`\r` 被忽略。
- 不存在文本布局框，`width` 和 `height` 不约束文本。
- `horizontalAlign=left` 时 X 是视觉左边；`center` 时 X 是视觉中心锚点；`right` 时 X 是视觉右边锚点。
- 为与当前渲染器完全一致，若不换行文本使用负 `width`，上述锚点先取 `x + width`；示例 JSON 的文本宽度均为正值，正式模板也应保持正值以避免这种兼容行为。

`character`：

- `width` 启用文本布局框。
- 按 UTF-8 Unicode code point 逐字符尝试放入当前行。
- 单个 glyph 比文本框还宽时允许该 glyph 超出框宽，因为字符本身无法继续拆分。

`smart`：

- `width` 启用文本布局框。
- ASCII 字母、数字、下划线和英文撇号组成单词，优先保持在同一行。
- 极长 ASCII 单词会按字符回退拆分；单次测量 token 最多 64 字节，防止 U8g2 的 16 位宽度溢出。
- 普通空格、Tab、全角空格在自动换行处折叠；新行开头和行末不保留多余空格。
- 显式 `\n` 始终强制换行，空行也必须保留。
- 左括号/左引号等不得留在行尾：`（《【『「〔〈([{“‘` 与后一个非空白 token 组合。
- 逗号、句号、右括号等不得出现在行首：`，。！？；：、）》】』」〕〉…—,.!?;:%)]}”’` 优先与前一字符同组；必要时把前一字符一并移到下一行。

### 6.2 文本布局框

当 `wrapMode != none`：

```text
boxWidth = max(1, abs(width))
boxLeft  = width >= 0 ? x : x + width
boxTop   = builtin 字体 ? y : y - fontAscent
```

因此，对 U8g2/FreeFonts 而言，JSON 的 `y` 仍是首行字体基线参考，不是布局框顶边。这个规则用于保证从旧的不换行元素切换到换行布局时，第一行不会突然上下跳动。

### 6.3 行高和自动高度

```text
lineAdvance  = fontLineHeight + lineSpacing
contentHeight = fontLineHeight + (lineCount - 1) * lineAdvance
```

- `autoHeight=true`：实际布局框高度等于完整 `contentHeight`，JSON `height` 被忽略，所有行参与布局；画布外部分自然不可见。
- `autoHeight=false`：实际框高为 `max(abs(height), fontLineHeight)`。
- 固定高度能容纳的完整行数为 `max(1, (boxHeight + lineSpacing) / lineAdvance)`，只绘制前 N 行；不自动添加省略号。

### 6.4 水平对齐

对每一行使用该行的真实像素宽度独立计算：

- `left`：视觉左边 = `boxLeft`
- `center`：视觉左边 = `boxLeft + (boxWidth - lineWidth) / 2`
- `right`：视觉左边 = `boxLeft + boxWidth - lineWidth`

多行文本不是整体一次居中，而是每行分别对齐。

### 6.5 垂直对齐

仅在 `wrapMode != none` 且固定框高存在剩余空间时有效：

- `top`：偏移 0
- `middle`：偏移 `(boxHeight - visibleContentHeight) / 2`
- `bottom`：偏移 `boxHeight - visibleContentHeight`

`autoHeight=true` 时框高等于内容高，因此三种垂直对齐通常视觉相同。

## 7. 示例 JSON 的 DataModel 绑定表

JSON 中的坐标、字体、框宽、对齐、换行和图层顺序属于设计模板，必须原样保留。正式渲染时只替换表中标记的动态文本，或在明确的天气图标占位区域执行自定义绘制。

绑定时使用 `id`，并断言 `name` 与预期一致。不要使用数组下标，因为 ID 5 已被删除。

| ID | 名称 | 正式值来源/行为 | 精确格式或说明 |
|---:|---|---|---|
| 1 | 页面边框 | 静态 | 原样绘制。 |
| 2 | 日期 | `WeatherData.realtime.serverTime + tzShiftSeconds` | 当地日期，格式 `Y/M/D`，月和日不补零，例如 `2026/7/18`。 |
| 3 | 分割线 | 静态 | 原样绘制。 |
| 4 | 天气现象文本 | `realtime.skyCondition` | 将彩云 skycon code 映射为中文，例如 `CLEAR_DAY -> 晴`。 |
| 6 | 更新时间 | `realtime.serverTime + tzShiftSeconds` | `已更新:%02d:%02d:%02d`。 |
| 7 | 天气现象 | 当前天气图标占位 | JSON 本身只定义 50×50 黑色矩形。要完全复现设计器预览就保留矩形；正式项目可在同一位置按 `realtime.skyCondition` 绘制 1-bit 图标。 |
| 8 | 预报区域分割 | 静态 | 原样绘制。 |
| 9 | 当前气温 | `realtime.temperature` | 四舍五入整数，`%d℃`。这里使用中文 `℃`，不是两个 ASCII 字符。 |
| 10 | 全天温度范围 | 当地今天对应的 `DailyWeatherItem` | `最大温°C - 最小温°C`，两侧保留空格，例如 `31°C - 25°C`。 |
| 11 | 预报分割线L | 静态 | 原样绘制。 |
| 12 | 预报分割线R | 静态 | 原样绘制。 |
| 13 | 天气描述 | `WeatherData.forecastKeypoint` | 原字符串，不截断；为空时可回退 `hourly.description`。保留 `smart + center + autoHeight`。 |
| 14 | 明天天气气象 | 明天天气图标占位 | JSON 本身为 25×25 黑色矩形；正式图标优先使用明日 `skyCondition`，缺失时使用 `daytimeSkyCondition`。 |
| 15 | 明天 | 静态标签 | 保持 `明天`。 |
| 16 | 明天气温范围 | 明日 `DailyWeatherItem` | `最小温°C-最大温°C`，无空格，例如 `17°C-23°C`。注意该图层的顺序与 ID 10 不同，必须按示例格式。 |
| 17 | 室内温度 | 独立室内传感器 DataModel | `室内温度 %d°C`。该字段不在 `WeatherData` 内，不得误用室外 `realtime.temperature`。 |
| 18 | 室内湿度 | 独立室内传感器 DataModel | `室内湿度 %d %%`，百分号前保留一个空格。该字段不在 `WeatherData` 内。 |
| 19 | 明天湿度 | 明日 `averageHumidity` | 模型范围为 0.0–1.0，先乘 100 再四舍五入，格式 `%d%%`。 |
| 20 | 明天降雨概率 | 明日 `precipitationProbability` | 模型已是 0–100，格式 `降雨概率 %d%%`。 |
| 21 | 明天天气现象文本 | 明日 `skyCondition` | 映射为中文；25px 窄框和智能换行会形成紧凑多行文本，不得手工插入换行。 |
| 22 | 剩余电量 | 独立电源/电池 DataModel | `剩余电量 %d%%`。该字段不在 `WeatherData` 内。 |
| 23 | 本站气压 | `realtime.pressure` | 彩云模型值为 Pa，除以 100 转为 hPa 后四舍五入：`本站气压 %d hPa`。 |
| 24 | 后天 | 静态标签 | 保持 `后天`。 |
| 25 | 后天气温范围 | 后日 `DailyWeatherItem` | `最小温°C-最大温°C`，无空格。 |
| 26 | 后天天气现象 | 后天天气图标占位 | JSON 本身为 25×25 黑色矩形；正式图标优先使用后日 `skyCondition`。 |
| 27 | 后天天气现象文本 | 后日 `skyCondition` | 映射为中文，保留 25px 智能换行居中框。 |
| 28 | 后天湿度 | 后日 `averageHumidity` | 先乘 100，格式 `%d%%`。 |
| 29 | 后天降雨概率 | 后日 `precipitationProbability` | 格式 `降雨概率 %d%%`。 |

### 7.1 逐日数据选择

不要盲目假设 `daily.items[0]` 永远是今天。优先把 `realtime.serverTime + tzShiftSeconds` 转为当地日期，再按 `DailyWeatherItem.date` 的日期部分查找：

- 今天：当地日期 + 0 天
- 明天：当地日期 + 1 天
- 后天：当地日期 + 2 天

若 API 层已保证 `daily.items` 从今天开始连续排序，才可以使用 `[0]`、`[1]`、`[2]`，并且使用前必须检查 `items.size() >= 3`。

### 7.2 建议 skycon 中文映射

> 请以彩云 API 文档为准.

<https://docs.caiyunapp.com/weather-api/v2/v2.6/tables/skycon.html>

未知 code 不得直接输出过长的英文枚举值到中文窄框；使用 `未知` 或项目定义的短中文回退文本。

### 7.3 无效值和旧数据

- 先检查 `WeatherData::isAvailable()`。
- 当前 DataModel 的 API 刷新是原子替换设计；刷新失败时应继续显示上一份有效快照，不要用半份新数据覆盖 UI。
- 不得格式化 `NAN`、空字符串或 `-1` 概率。
- 如果启动时从未取得有效数据，使用短占位符，例如 `--℃`、`--%`、`本站气压 -- hPa`，同时保留所有布局属性。
- 室内温湿度和电量必须各自检查其独立 DataModel 的有效状态。

## 8. 推荐的运行时绑定结构

布局模板与运行时数据应分离。不要把 API 值写回 `assets/epaper-ui-design.json`。

```cpp
UiDocument layoutTemplate;

UiElement *findElementById(UiDocument &document, uint32_t id)
{
    for (UiElement &element : document.elements)
    {
        if (element.id == id) return &element;
    }
    return nullptr;
}

void bindText(UiDocument &document, uint32_t id, const char *expectedName, const String &value)
{
    UiElement *element = findElementById(document, id);
    if (element == nullptr || element->type != UiElementType::Text || element->name != expectedName)
    {
        // 记录布局版本不匹配；不要悄悄绑定到其他图层。
        return;
    }
    element->text = value; // 只替换内容，不改坐标、字体、宽高或对齐属性。
}

void renderWeatherFrame(Adafruit_GFX &target, const WeatherData &weather)
{
    UiDocument runtime = layoutTemplate;
    // 按第 7 节格式化并 bindText(runtime, id, name, value)。
    drawUiDocument(target, runtime);
}
```

实际 ESP32 代码可为了减少复制而原地更新文本，但必须保证每次数据刷新都覆盖所有动态字段，不能残留上一次天气的字符串。

### 8.1 天气图标扩展

当前 JSON schema 没有 bitmap/icon 类型，ID 7、14、26 是占位矩形。若正式项目要绘制真实图标：

1. 保留这三个元素的 ID、位置、宽高和 z-order。
2. 在 `drawUiDocument()` 遍历到对应 ID 时，用 skycon 图标绘制函数替代矩形分支。
3. 图标必须为 1-bit，并限制在原占位框：ID 7 为 50×50；ID 14、26 为 25×25。
4. 不要在整份文档绘制完成后统一补图标，否则可能破坏后续图层覆盖关系。
5. 若暂未实现图标映射，保留黑色矩形即可精确复现当前设计器预览。

## 9. 加载和刷新顺序

推荐流程：

1. 启动时从编译期资源或 LittleFS 读取 `epaper-ui-design.json`。
2. 调用与设计器一致的 `deserializeUiDocument()`；检查 version、画布尺寸、元素数量、字体和类型。
3. 校验所有绑定 ID 与名称。
4. 获取或恢复最后一份有效 DataModel。
5. 复制模板并只替换动态值。
6. `display.setFullWindow()`。
7. 在 `firstPage()/nextPage()` 的每一轮中对相同的运行时文档调用 `drawUiDocument()`。
8. 刷新结束后 `display.hibernate()`。

不要在 `firstPage()/nextPage()` 循环内部重新读取传感器、重新请求网络或改变字符串，否则不同 page pass 可能绘制出不同帧内容。

## 10. 必须通过的验收检查

另一个 Codex 在宣称复现完成前，至少完成以下检查：

- [ ] 使用 `GxEPD2_420_GDEY042T81`、400×300、rotation 2。
- [ ] 使用同一份 `epaper-ui-design.json`，元素数为 28，保留缺失的 ID 5。
- [ ] 直接复用或逐条等价实现 `UiDocument` 与 `UiRenderer`。
- [ ] 编译并使用相同的 `pingfang_medium32_gb2312.h`。
- [ ] U8g2 文本使用 `getUTF8Width()`，GFX 文本使用 `getTextBounds()`。
- [ ] `target.setTextWrap(false)`，没有启用库自带自动换行。
- [ ] `天气描述` 在 250px 框内智能换行、逐行居中、自动增高。
- [ ] `重度雾霾` 等文本在 25px 框内自动形成多行，而不是缩小字体或裁成一行。
- [ ] 中文标点不出现在行首，左括号不留在行尾。
- [ ] ID 17、18、22 不错误绑定到室外天气数据。
- [ ] 湿度执行 `0.0–1.0 × 100`，气压执行 `Pa ÷ 100 -> hPa`，降雨概率不再乘 100。
- [ ] 数值格式、空格和 `℃`/`°C` 的选择与第 7 节一致。
- [ ] JSON 数组顺序就是实际绘制顺序。
- [ ] DataModel 在进入 page loop 前冻结为一份快照。
- [ ] PlatformIO 编译通过。
- [ ] 有硬件时完成一次实体屏全刷；无硬件时至少在 400×300 `GFXcanvas1` 上生成 1-bit 结果。

若设计器设备可运行，最强验收方式是：导入同一 JSON，读取 `/preview.bmp` 作为黄金图；目标项目用 `GFXcanvas1` 渲染同一静态示例，逐像素比较两张 400×300 1-bit 图。动态 DataModel 测试则固定一份输入快照后再生成黄金图，避免时间和传感器值变化。

## 11. 禁止的近似实现

以下做法无法完整复现，禁止作为最终实现：

- 使用浏览器 Canvas、系统字体或 OTF 直接渲染预览。
- 用字符数量乘字号估算中文/符号宽度。
- 使用 Adafruit_GFX 自带 `setTextWrap(true)` 代替智能换行。
- 把 U8g2 的 Y 当作文本顶部。
- 对整段多行文本只计算一次居中，而不是逐行对齐。
- 忽略 `autoHeight`、`lineSpacing`、`verticalAlign` 或中文标点禁排。
- 按 JSON 数组下标绑定天气字段。
- 每次数据变化都修改 JSON 的布局属性。
- 把 `realtime.pressure` 的 Pa 数值直接标成 hPa。
- 把 `averageHumidity` 再当作 0–100 使用，或把已是 0–100 的降雨概率再次乘 100。
- 在 page loop 中读取变化中的 DataModel。

只要目标任务遵循本契约并复用权威渲染器，同一静态 JSON 应得到与设计器 `/preview.bmp` 一致的 1-bit 排版结果；正式项目只会因 DataModel 值和可选天气图标不同而改变内容，不应改变布局规则。
