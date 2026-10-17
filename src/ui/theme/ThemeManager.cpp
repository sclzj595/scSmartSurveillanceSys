// ThemeManager.cpp —— QSS 由令牌生成（单套参数化模板，双主题同规则）
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QWidget>
#include <QMap>

#ifdef Q_OS_WIN
#include "ui/WinTheme.h"
#endif

using uitheme::ThemeTokens;

namespace {
inline QString hex(const QColor& c) { return c.name(); }  // #rrggbb
}  // namespace

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

void ThemeManager::apply(const QString& themeName)
{
    _tokens = uitheme::byName(themeName);
    qApp->setStyleSheet(buildQss(_tokens));
    emit themeChanged(_tokens.name);
}

void ThemeManager::applyNative(QWidget* widget) const
{
#ifdef Q_OS_WIN
    if (!widget)
        return;
    const bool dark = _tokens.name == QStringLiteral("dark");
    wintheme::applyDarkTitleBar(reinterpret_cast<HWND>(widget->winId()), dark,
        RGB(_tokens.nativeCaption.red(),   _tokens.nativeCaption.green(),   _tokens.nativeCaption.blue()),
        RGB(_tokens.nativeText.red(),      _tokens.nativeText.green(),      _tokens.nativeText.blue()),
        RGB(_tokens.nativeBorder.red(),    _tokens.nativeBorder.green(),    _tokens.nativeBorder.blue()));
#else
    Q_UNUSED(widget);
#endif
}

// 模板规范：颜色一律 @token@ 占位；圆角/间距/字号取 DesignTokens 刻度
// （RadiusSm=4 / RadiusMd=6 / RadiusLg=8 / RadiusPill=11；字号 12/13/15）
QString ThemeManager::buildQss(const ThemeTokens& t)
{
    const QMap<QString, QString> tok = {
        {"bgBase", hex(t.bgBase)},           {"bgPanel", hex(t.bgPanel)},
        {"bgElevated", hex(t.bgElevated)},   {"bgHover", hex(t.bgHover)},
        {"bgActive", hex(t.bgActive)},       {"bgSunken", hex(t.bgSunken)},
        {"bgVideo", hex(t.bgVideo)},         {"bgInput", hex(t.bgInput)},
        {"bgChip", hex(t.bgChip)},
        {"border", hex(t.border)},           {"borderStrong", hex(t.borderStrong)},
        {"borderHover", hex(t.borderHover)}, {"borderFocus", hex(t.borderFocus)},
        {"textPrimary", hex(t.textPrimary)},     {"textSecondary", hex(t.textSecondary)},
        {"textMuted", hex(t.textMuted)},         {"textDisabled", hex(t.textDisabled)},
        {"accent", hex(t.accent)},
        {"ok", hex(t.ok)},   {"okBg", hex(t.okBg)},     {"okBorder", hex(t.okBorder)},
        {"warn", hex(t.warn)}, {"warnBg", hex(t.warnBg)}, {"warnBorder", hex(t.warnBorder)},
        {"danger", hex(t.danger)}, {"dangerBorder", hex(t.dangerBorder)},
    };

    QString qss = R"(
/* ===== 全局基调 ===== */
* { outline: none; }
QWidget {
    background-color: @bgBase;
    color: @textSecondary;
    font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;
    font-size: 13px;
}
QLabel { background: transparent; }
QToolTip {
    background-color: @bgElevated; color: @textPrimary;
    border: 1px solid @borderStrong; border-radius: 4px;
    padding: 4px 8px; font-size: 12px;
}

/* ===== 标题栏 ===== */
QWidget#headerBar { background-color: @bgPanel; border-bottom: 1px solid @border; }
QLabel#logoChip { background-color: @accent; border-radius: 1px; }
QLabel#appTitle { color: @textPrimary; font-size: 15px; font-weight: 600; }
QLabel#srcChip, QLabel#svcChip {
    border: 1px solid @borderStrong; border-radius: 11px;
    padding: 2px 10px; color: @textMuted; background-color: @bgChip;
    font-size: 12px;
}
QLabel#srcChip[state="ok"], QLabel#svcChip[state="ok"] { color: @ok; border-color: @okBorder; }
QLabel#svcChip[state="bad"] { color: @danger; border-color: @dangerBorder; }

/* ===== 视频面板 ===== */
QLabel#videoLabel { background-color: @bgVideo; border: 1px solid @borderStrong; border-radius: 8px; }
QWidget#btnBar { background: transparent; }

/* ===== 按钮：中性默认，状态色只由 state/checked 语义驱动 ===== */
QPushButton {
    background-color: @bgElevated; color: @textSecondary;
    border: 1px solid @borderStrong; border-radius: 6px;
    padding: 7px 16px; min-width: 88px;
}
QPushButton:hover  { background-color: @bgHover; border-color: @borderHover; }
QPushButton:pressed { background-color: @bgActive; }
QPushButton:disabled { color: @textDisabled; border-color: @border; background-color: @bgBase; }
QPushButton[state="on"] { color: @ok; border-color: @okBorder; background-color: @okBg; }
QPushButton[state="on"]:hover { border-color: @ok; }
QPushButton:checked { color: @warn; border-color: @warnBorder; background-color: @warnBg; }
QPushButton#themeToggleBtn { min-width: 0; padding: 5px 12px; color: @textMuted; font-size: 12px; }
QPushButton#themeToggleBtn:hover { color: @textSecondary; }

/* ===== 状态栏 ===== */
QLabel#logLabel {
    background-color: @bgSunken; color: @textMuted;
    border-top: 1px solid @border; padding: 0 14px;
}

/* ===== 弹窗（注册/切源/历史/消息框）===== */
QDialog, QMessageBox { background-color: @bgPanel; }
QMessageBox QLabel { background: transparent; }
QMessageBox QPushButton { min-width: 72px; }
QLabel#previewLabel { border: 1px solid @borderStrong; border-radius: 6px; background: @bgSunken; }

/* ===== 输入控件 ===== */
QLineEdit, QComboBox {
    background-color: @bgInput; color: @textSecondary;
    border: 1px solid @borderStrong; border-radius: 6px; padding: 6px 10px;
    selection-background-color: @accent; selection-color: #ffffff;
}
QLineEdit:focus, QComboBox:focus { border-color: @borderFocus; }
QLineEdit:disabled, QComboBox:disabled { color: @textDisabled; background-color: @bgSunken; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background-color: @bgElevated; border: 1px solid @borderStrong;
    selection-background-color: @accent; selection-color: #ffffff;
}

/* ===== 历史记录表格 ===== */
QTableWidget {
    background-color: @bgBase; color: @textSecondary;
    border: 1px solid @borderStrong; border-radius: 6px;
    gridline-color: @border;
    selection-background-color: @accent; selection-color: #ffffff;
}
QTableWidget::item { padding: 4px; }
QHeaderView::section {
    background-color: @bgPanel; color: @textMuted;
    border: none; border-bottom: 1px solid @borderStrong; padding: 6px 8px;
}

/* ===== 滚动条（双轴，纤细圆角）===== */
QScrollBar:vertical { background: @bgBase; width: 10px; }
QScrollBar::handle:vertical { background: @borderStrong; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @borderHover; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: @bgBase; height: 10px; }
QScrollBar::handle:horizontal { background: @borderStrong; border-radius: 5px; min-width: 30px; }
QScrollBar::handle:horizontal:hover { background: @borderHover; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
)";

    for (auto it = tok.constBegin(); it != tok.constEnd(); ++it)
        qss.replace(QLatin1Char('@') + it.key() + QLatin1Char('@'), it.value());
    return qss;
}
