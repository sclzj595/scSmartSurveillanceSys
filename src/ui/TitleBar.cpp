// TitleBar.cpp —— 红绿灯窗控与拖拽/最大化交互
#include "ui/TitleBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QWindow>

// 红绿灯圆钮：常驻 Mac 系统色（平台语义常量，不参与主题令牌）；
// ×/−/+ 字形常驻但透明，悬停时显现（对齐 macOS 行为）
class LightButton : public QToolButton
{
public:
    LightButton(const QString& objectName, const QString& glyph, const QString& tip)
    {
        setObjectName(objectName);
        setText(glyph);
        setFixedSize(14, 14);
        setToolTip(tip);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }
};

TitleBar::TitleBar(QWidget* window)
    : QWidget(window), _win(window)
{
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(46);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 0, 14, 0);
    lay->setSpacing(8);

    _close = new LightButton(QStringLiteral("lightClose"), QStringLiteral("\u00d7"),
                             tr("关闭"));
    _min   = new LightButton(QStringLiteral("lightMin"),   QStringLiteral("\u2212"),
                             tr("最小化"));
    _max   = new LightButton(QStringLiteral("lightMax"),   QStringLiteral("+"),
                             tr("最大化 / 还原"));
    lay->addWidget(_close);
    lay->addWidget(_min);
    lay->addWidget(_max);
    lay->addSpacing(8);

    auto* title = new QLabel(QStringLiteral("智能安防监控"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    lay->addWidget(title);
    lay->addStretch();   // addRight 一律插到 stretch 之前（保持右侧聚拢）

    connect(_close, &QToolButton::clicked, _win, &QWidget::close);
    connect(_min,   &QToolButton::clicked, _win, &QWidget::showMinimized);
    connect(_max,   &QToolButton::clicked, this, [this] {
        _win->isMaximized() ? _win->showNormal() : _win->showMaximized();
    });
}

void TitleBar::addRight(QWidget* w)
{
    auto* l = static_cast<QHBoxLayout*>(layout());
    l->insertWidget(l->count() - 1, w);
}

void TitleBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && _win->windowHandle())
        _win->windowHandle()->startSystemMove();   // 系统级拖拽：Aero Snap 原生可用
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        _win->isMaximized() ? _win->showNormal() : _win->showMaximized();
    QWidget::mouseDoubleClickEvent(e);
}
