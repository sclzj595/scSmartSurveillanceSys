// ThemeManager.h —— 主题单点管理（观察者模式）
//
// v1.1 收敛：设计令牌/DWM 标题栏实现迁至套件共享库 mvcore（Theme.h 单点，
// FaceGate 同源）；本类保留为宿主适配层——运行时切换 + themeChanged 广播 +
// SS 专属控件段 QSS（titleBar/chips/dockBar/logLabel 等本仓控件清单）。
// 业务代码不直接 setStyleSheet / 不写十六进制色值，一律走令牌。
#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <mvcore/Theme.h>

class QWidget;

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager& instance();

    void apply(const QString& themeName);              // "dark" / "light"
    // 注：不用 using 别名——moc 无法解析 `using X = mvcore::uitheme;`，
    // 成员声明一律写全限定名
    const mvcore::uitheme::ThemeTokens& tokens() const { return _tokens; }
    QString themeName() const { return _tokens.name; }

    // 原生标题栏按当前令牌配色（对话框等非自绘窗用；委托 mvcore::wintheme）
    void applyNative(QWidget* widget) const;

signals:
    void themeChanged(const QString& name);

private:
    ThemeManager() = default;
    static QString buildQss(const mvcore::uitheme::ThemeTokens& t);  // @token@ 占位符替换

    mvcore::uitheme::ThemeTokens _tokens = mvcore::uitheme::dark();
};

#endif  // THEMEMANAGER_H
