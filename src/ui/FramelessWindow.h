// FramelessWindow.h —— 无边框窗基类（Mac 风自绘标题栏的窗体承载）
//
// 方案：WM_NCCALCSIZE 剥离原生标题栏，但保留 WS_THICKFRAME 窗口样式——
// 原生缩放边、Aero Snap、最小化动画、Win11 圆角全部无损保留；
// WM_NCHITTEST 提供 8px 缩放热区；拖拽/双击最大化由 TitleBar 承担
// （windowHandle()->startSystemMove()，系统级实现，Snap 正常触发）。
// 好处：原生标题栏彻底消失，Win10 上深浅主题与系统色错配的问题连根拔掉。
#ifndef FRAMELESSWINDOW_H
#define FRAMELESSWINDOW_H

#include <QWidget>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

class FramelessWindow : public QWidget
{
    Q_OBJECT
public:
    explicit FramelessWindow(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setWindowFlag(Qt::FramelessWindowHint, true);
    }

protected:
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override
    {
        MSG* m = static_cast<MSG*>(message);
        // 缩放热区/最大化出界补偿：固定 8px（MinGW 8.1 头文件缺 SM_CXPADDEDBORDER，
        // SM_CXFRAME 又偏薄；8px 是 DWM 视觉边框的实测值）
        if (m->message == WM_NCCALCSIZE && m->wParam) {
            if (IsZoomed(m->hwnd)) {
                auto* r = reinterpret_cast<RECT*>(m->lParam);
                r->left += 8; r->top += 8; r->right -= 8; r->bottom -= 8;
            }
            *result = 0;
            return true;
        }
        // 8px 缩放热区（最大化时不提供，避免误触发）
        if (m->message == WM_NCHITTEST && !IsZoomed(m->hwnd)) {
            const int x = GET_X_LPARAM(m->lParam), y = GET_Y_LPARAM(m->lParam);
            RECT rc;
            GetWindowRect(m->hwnd, &rc);
            const int band = 8;
            const bool atL = x < rc.left + band,  atR = x >= rc.right - band;
            const bool atT = y < rc.top + band,   atB = y >= rc.bottom - band;
            if (atL && atT) { *result = HTTOPLEFT;    return true; }
            if (atR && atT) { *result = HTTOPRIGHT;   return true; }
            if (atL && atB) { *result = HTBOTTOMLEFT; return true; }
            if (atR && atB) { *result = HTBOTTOMRIGHT;return true; }
            if (atL) { *result = HTLEFT;   return true; }
            if (atR) { *result = HTRIGHT;  return true; }
            if (atT) { *result = HTTOP;    return true; }
            if (atB) { *result = HTBOTTOM; return true; }
        }
        return QWidget::nativeEvent(eventType, message, result);
    }
#endif
};

#endif  // FRAMELESSWINDOW_H
