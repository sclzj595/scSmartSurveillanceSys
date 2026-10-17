// WinTheme.h —— Windows 原生标题栏主题（DWM）
//
// 解决：深色界面配亮色原生标题栏的割裂感。
// 选型说明：DWM API 方案保留系统窗口全部原生行为（缩放/贴靠/最大化动画/多显示器），
// 优于 frameless 自绘标题栏（后者要自己处理命中测试、resize 边框、Snap 布局，得不偿失）。
//
// 能力分级：
//   Win10 1809+  DWMWA_USE_IMMERSIVE_DARK_MODE(20)：标题栏深色模式（属性 19 为早期预览值，双发兼容）
//   Win11 22000+ DWMWA_CAPTION_COLOR(35)/TEXT(36)/BORDER(34)：精确配色，与主题面板色一致
//   更早系统     两次调用均失败，静默跳过（标题栏维持系统默认，不影响运行）
//
// 头文件仅依赖 windows/dwmapi + Qt 基础头，可被 MainWindow 与各弹窗复用。
#ifndef WINTHEME_H
#define WINTHEME_H

#include <QtGlobal>

#ifdef Q_OS_WIN

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

namespace wintheme {

// 深浅主题统一入口：显式传入配色（由 ThemeManager 从令牌换算，本文件不持色值）
inline void applyDarkTitleBar(HWND hwnd, bool dark,
                              COLORREF caption, COLORREF text, COLORREF border)
{
    if (!hwnd)
        return;
    const BOOL v = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE（1809 起以 19 预览存在，双发确保命中）
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &v, sizeof v)))
        DwmSetWindowAttribute(hwnd, 19, &v, sizeof v);

    // Win11 22000+：标题栏/文字/边框精确配色（Win10 返回错误码，静默忽略）
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof caption);   // DWMWA_CAPTION_COLOR
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof text);         // DWMWA_TEXT_COLOR
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof border);     // DWMWA_BORDER_COLOR
}

// 缺省配色版（无 ThemeManager 场景；正常路径请走 ThemeManager::applyNative）
inline void applyDarkTitleBar(HWND hwnd, bool dark)
{
    const COLORREF cap = dark ? RGB(0x1b, 0x22, 0x2c) : RGB(0xff, 0xff, 0xff);
    const COLORREF txt = dark ? RGB(0xde, 0xe3, 0xec) : RGB(0x1f, 0x24, 0x30);
    const COLORREF brd = dark ? RGB(0x26, 0x2e, 0x3a) : RGB(0xe2, 0xe7, 0xee);
    applyDarkTitleBar(hwnd, dark, cap, txt, brd);
}

// QWidget 便捷重载（winId() 会强制创建原生窗口，构造后调用安全）
template <typename W>
inline void applyDarkTitleBar(W* widget, bool dark)
{
    applyDarkTitleBar(reinterpret_cast<HWND>(widget->winId()), dark);
}

} // namespace wintheme

#endif // Q_OS_WIN
#endif // WINTHEME_H
