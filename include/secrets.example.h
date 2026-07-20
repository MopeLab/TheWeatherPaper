#pragma once

// ============================================================
// Wi-Fi
// ============================================================

inline constexpr char WIFI_SSID[] =
    "替换为你的WiFi名称";

inline constexpr char WIFI_PASSWORD[] =
    "替换为你的WiFi密码";

// ============================================================
// 彩云天气认证方式
// ============================================================

// true：使用推荐的 App Key + App Secret 签名认证
// false：使用旧 Token 认证
inline constexpr bool CAIYUN_USE_SIGNED_AUTH = true;

// 推荐认证方式
inline constexpr char CAIYUN_APP_KEY[] =
    "替换为你的App Key";

inline constexpr char CAIYUN_APP_SECRET[] =
    "替换为你的App Secret";

// 旧 Token 认证方式；不用时可以留空
inline constexpr char CAIYUN_TOKEN[] =
    "";

// ============================================================
// 天气地点
// ============================================================

// 彩云请求路径的顺序必须是：经度,纬度
// 建议把数值保存为字符串，避免浮点格式发生不必要变化。
inline constexpr char LOCATION_LONGITUDE[] =
    "118.0894";

inline constexpr char LOCATION_LATITUDE[] =
    "24.4798";

// 本课仍使用 Adafruit GFX 内置英文字体，因此使用 ASCII。
inline constexpr char LOCATION_NAME[] =
    "XIAMEN";
