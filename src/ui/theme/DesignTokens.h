// DesignTokens.h —— 设计令牌（UI 规范的单点事实来源）
//
// 原则（对应 docs/UI设计规范.md）：
//   1. 颜色只在此处与 ThemeManager 的 QSS 模板出现，业务代码禁止散落十六进制字面量
//   2. 视频叠加层绘制在"始终为暗"的画面上，调色板与主题无关（OverlayPalette）
//   3. 圆角/间距/字号使用固定刻度，不出现魔法数
#ifndef DESIGNTOKENS_H
#define DESIGNTOKENS_H

#include <QColor>
#include <QString>

namespace uitheme {

// ---- 主题令牌：语义化分组，QSS 模板按名引用 ----
struct ThemeTokens
{
    QString name;   // "dark" | "light"

    // 表面（由深到浅：窗口底 → 面板 → 浮起控件 → 悬停 → 按下）
    QColor bgBase;       // 窗口底
    QColor bgPanel;      // 面板（标题栏/弹窗）
    QColor bgElevated;   // 浮起（按钮/下拉弹层）
    QColor bgHover;
    QColor bgActive;     // 按下
    QColor bgSunken;     // 凹陷（状态栏/预览底）
    QColor bgVideo;      // 视频区（恒黑）
    QColor bgInput;      // 输入控件
    QColor bgChip;       // 标题栏状态 chip 底

    // 边框
    QColor border;        // 弱分隔
    QColor borderStrong;  // 控件描边
    QColor borderHover;
    QColor borderFocus;   // = accent

    // 文本
    QColor textPrimary;    // 标题
    QColor textSecondary;  // 正文/按钮
    QColor textMuted;      // 次要（chips/日志/表头）
    QColor textDisabled;

    // 语义色（状态唯一来源：ok=绿 / warn=琥珀 / danger=红 / accent=蓝）
    QColor accent;
    QColor ok, okBg, okBorder;
    QColor warn, warnBg, warnBorder;
    QColor danger, dangerBorder;

    // Windows 原生标题栏（DWM 配色与面板无缝衔接）
    QColor nativeCaption, nativeText, nativeBorder;
};

// ---- 圆角刻度 ----
enum : int { RadiusSm = 4, RadiusMd = 6, RadiusLg = 8, RadiusPill = 11 };
// ---- 间距刻度（8px 网格 + 半步）----
enum : int { Space1 = 4, Space2 = 8, Space3 = 12, Space4 = 16, Space5 = 24 };
// ---- 字号刻度 ----
enum : int { FontXs = 12, FontSm = 13, FontMd = 15, FontLg = 16 };

inline const ThemeTokens& dark()
{
    static const ThemeTokens t = {
        "dark",
        /*bgBase*/      {0x15,0x1a,0x21},
        /*bgPanel*/     {0x1b,0x22,0x2c},
        /*bgElevated*/  {0x22,0x2b,0x37},
        /*bgHover*/     {0x2a,0x35,0x43},
        /*bgActive*/    {0x1f,0x28,0x33},
        /*bgSunken*/    {0x10,0x14,0x1a},
        /*bgVideo*/     {0x06,0x08,0x09},
        /*bgInput*/     {0x10,0x14,0x1a},
        /*bgChip*/      {0x20,0x28,0x34},
        /*border*/        {0x26,0x2e,0x3a},
        /*borderStrong*/  {0x2e,0x3a,0x49},
        /*borderHover*/   {0x3d,0x4c,0x60},
        /*borderFocus*/   {0x3b,0x82,0xf6},
        /*textPrimary*/   {0xf0,0xf4,0xfa},
        /*textSecondary*/ {0xdd,0xe3,0xec},
        /*textMuted*/     {0x97,0xa2,0xb2},
        /*textDisabled*/  {0x5c,0x66,0x73},
        /*accent*/        {0x3b,0x82,0xf6},
        /*ok*/        {0x4a,0xde,0x80}, /*okBg*/ {0x18,0x22,0x19}, /*okBorder*/ {0x2f,0x5e,0x46},
        /*warn*/      {0xfb,0xbf,0x24}, /*warnBg*/ {0x24,0x1d,0x10}, /*warnBorder*/ {0x8a,0x5a,0x12},
        /*danger*/    {0xf8,0x71,0x71}, /*dangerBorder*/ {0x5b,0x2c,0x2c},
        /*nativeCaption*/ {0x1b,0x22,0x2c},
        /*nativeText*/    {0xde,0xe3,0xec},
        /*nativeBorder*/  {0x26,0x2e,0x3a},
    };
    return t;
}

inline const ThemeTokens& light()
{
    static const ThemeTokens t = {
        "light",
        /*bgBase*/      {0xf3,0xf5,0xf8},
        /*bgPanel*/     {0xff,0xff,0xff},
        /*bgElevated*/  {0xff,0xff,0xff},
        /*bgHover*/     {0xf0,0xf4,0xf9},
        /*bgActive*/    {0xe6,0xec,0xf3},
        /*bgSunken*/    {0xff,0xff,0xff},
        /*bgVideo*/     {0x06,0x08,0x09},
        /*bgInput*/     {0xff,0xff,0xff},
        /*bgChip*/      {0xf6,0xf8,0xfb},
        /*border*/        {0xe2,0xe7,0xee},
        /*borderStrong*/  {0xd5,0xdc,0xe5},
        /*borderHover*/   {0xb9,0xc4,0xd2},
        /*borderFocus*/   {0x3b,0x82,0xf6},
        /*textPrimary*/   {0x11,0x18,0x27},
        /*textSecondary*/ {0x37,0x41,0x51},
        /*textMuted*/     {0x5b,0x66,0x75},
        /*textDisabled*/  {0xa8,0xb1,0xbd},
        /*accent*/        {0x3b,0x82,0xf6},
        /*ok*/        {0x15,0x80,0x3d}, /*okBg*/ {0xef,0xfa,0xf3}, /*okBorder*/ {0x87,0xc9,0xa0},
        /*warn*/      {0xb4,0x53,0x09}, /*warnBg*/ {0xfd,0xf6,0xe7}, /*warnBorder*/ {0xe3,0xb8,0x77},
        /*danger*/    {0xb9,0x1c,0x1c}, /*dangerBorder*/ {0xf0,0xc4,0xc4},
        /*nativeCaption*/ {0xff,0xff,0xff},
        /*nativeText*/    {0x1f,0x24,0x30},
        /*nativeBorder*/  {0xe2,0xe7,0xee},
    };
    return t;
}

inline const ThemeTokens& byName(const QString& name)
{
    return name == QStringLiteral("light") ? light() : dark();
}

// ---- 视频叠加层调色板：绘制在恒黑画面上，双主题共用 ----
// 语义：绿=已知 / 黄=陌生 / 琥珀=遗留确认 / 红=禁区·入侵 / 灰=跟踪中
struct OverlayPalette
{
    static inline const QColor knownFace   {0x22,0xc5,0x5e};
    static inline const QColor unknownFace {0xea,0xb3,0x08};
    static inline const QColor leftObject  {0xf5,0x9e,0x0b};
    static inline const QColor trackBox    {0x9c,0xa3,0xaf};
    static inline const QColor zone        {0xef,0x44,0x44};
    static inline const QColor intrusion   {0xdc,0x26,0x26};
    static inline const QColor hudText     {0x34,0xd3,0x99};
    static inline const QColor labelScrim  {0x00,0x00,0x00,0xa5};
    static inline const QColor banner      {0xdc,0x26,0x26,0xc8};
    static inline const QColor white       {0xff,0xff,0xff};
};

}  // namespace uitheme

#endif  // DESIGNTOKENS_H
