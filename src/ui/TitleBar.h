// TitleBar.h —— Mac 风自绘标题栏
//
// 结构：[● ● ● 红绿灯窗控] [应用标题] ——stretch—— [右侧扩展区（chips/主题按钮）]
// 职责：窗体无边框后承担 拖拽移动（系统级 startSystemMove）/ 双击最大化 /
//       最小化 / 最大化还原 / 关闭；红绿灯样式与语义对齐 macOS。
#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>

class LightButton;
class QLabel;
class QPushButton;

class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget* window);
    void addRight(QWidget* w);   // 右侧追加控件（chips / 主题切换等）

protected:
    void mousePressEvent(QMouseEvent* e) override;        // 左键按下 → 系统级拖拽
    void mouseDoubleClickEvent(QMouseEvent* e) override;  // 双击 → 最大化/还原

private:
    QWidget* _win;
    LightButton* _close = nullptr;
    LightButton* _min   = nullptr;
    LightButton* _max   = nullptr;
};

#endif  // TITLEBAR_H
