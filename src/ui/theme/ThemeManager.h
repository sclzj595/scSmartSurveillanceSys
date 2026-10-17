// ThemeManager.h —— 主题单点管理（观察者模式）
//
// 职责：
//   1. 持有当前 ThemeTokens，apply() 切换主题并广播 themeChanged
//   2. 由令牌生成全局 QSS（单套参数化模板，双主题同规则）
//   3. applyNative()：Windows 原生标题栏配色与令牌同步
// 业务代码不直接 setStyleSheet / 不写十六进制色值，一律走令牌。
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include "ui/theme/DesignTokens.h"

class QWidget;

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager& instance();

    void apply(const QString& themeName);              // "dark" / "light"
    const uitheme::ThemeTokens& tokens() const { return _tokens; }
    QString themeName() const { return _tokens.name; }

    // 原生标题栏按当前令牌配色（非 Windows 空实现）
    void applyNative(QWidget* widget) const;

signals:
    void themeChanged(const QString& name);

private:
    ThemeManager() = default;
    static QString buildQss(const uitheme::ThemeTokens& t);  // @token@ 占位符替换

    uitheme::ThemeTokens _tokens = uitheme::dark();
};

#endif  // THEMEMANAGER_H
