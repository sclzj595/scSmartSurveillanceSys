// src/app/OverlayRenderer.cpp
// 画面叠加绘制（数据/呈现分离：引擎只给结构化结果，标注统一在这里）
// 视觉语言：AI 检测风格的"四角括号"框 + 圆角标签 chip，与主窗口工业监控主题同源
#include "OverlayRenderer.h"

QRect OverlayRenderer::toQRect(const cv::Rect& r)
{
    return QRect(r.x, r.y, r.width, r.height);
}

// 四角括号框：每角两条短线（AI 检测视觉语言；比全框更轻、更现代）
void OverlayRenderer::drawCornerBox(QPainter& p, const QRect& r,
                                    const QColor& color, int weight)
{
    const int len = qBound(10, qMin(r.width(), r.height()) / 4, 36);
    QPen pen(color, weight);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    const int L = r.left(), T = r.top(), R = r.right(), B = r.bottom();
    // 左上 / 右上 / 左下 / 右下
    p.drawLine(L, T + len, L, T);  p.drawLine(L, T, L + len, T);
    p.drawLine(R - len, T, R, T);  p.drawLine(R, T, R, T + len);
    p.drawLine(L, B - len, L, B);  p.drawLine(L, B, L + len, B);
    p.drawLine(R - len, B, R, B);  p.drawLine(R, B, R, B - len);
}

// 框上方的圆角标签 chip（中文由 QPainter 绘制，修复 OpenCV putText 乱码问题）
void OverlayRenderer::drawLabel(QPainter& p, const QRect& box,
                                const QString& text, const QColor& color)
{
    if (text.isEmpty())
        return;
    QFont f = p.font();
    f.setPointSize(11);
    f.setBold(true);
    p.setFont(f);
    const QFontMetrics fm(p.font());
    QRect tr = fm.boundingRect(text);
    int ty = box.top() - tr.height() - 8;
    if (ty < 2)
        ty = box.top() + 4;                 // 顶部放不下 → 移到框内
    const QRect bg(box.left(), ty, tr.width() + 14, tr.height() + 6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 165));
    p.drawRoundedRect(bg, 4, 4);
    p.setBrush(Qt::NoBrush);
    // 左侧色点 + 文字：状态色语义一眼可辨
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(color);
    p.drawEllipse(bg.left() + 6, bg.top() + bg.height() / 2 - 3, 6, 6);
    p.setBrush(Qt::NoBrush);
    p.setPen(color);
    p.drawText(bg.adjusted(15, 0, -5, 0), Qt::AlignCenter, text);
}

void OverlayRenderer::paint(QImage& img,
                            const std::vector<FaceResult>& faces,
                            const std::vector<TrackedObjectResult>& objects,
                            const IntrusionResult& intrusion,
                            const cv::Rect& zone,
                            const Hud& hud)
{
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 禁区：红色虚线全框（边界语义需要完整闭合，保留矩形）
    if (!zone.empty()) {
        const QColor zoneColor(0xef, 0x44, 0x44);
        p.setPen(QPen(zoneColor, 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(toQRect(zone));
        drawLabel(p, toQRect(zone), QStringLiteral("禁区"), zoneColor);
    }

    // 遗留物 / 跟踪目标：确认遗留 → 琥珀角括号加粗 + 标签；跟踪中 → 低调灰细角括号（FR-05 标注纪律）
    for (const TrackedObjectResult& obj : objects) {
        QRect qr = toQRect(obj.rect);
        if (obj.confirmedLeft) {
            const QColor amber(0xf5, 0x9e, 0x0b);
            drawCornerBox(p, qr, amber, 3);
            const qint64 minutes = qMax<qint64>(1, obj.durationMs / 60000);
            drawLabel(p, qr, QStringLiteral("遗留物 %1分钟").arg(minutes), amber);
        } else {
            drawCornerBox(p, qr, QColor(0x9c, 0xa3, 0xaf), 1);
        }
    }

    // 人脸：命中 → 绿角括号 + 档案名 + 相似度%；未命中 → 黄角括号 + 陌生人（FR-08）
    for (const FaceResult& face : faces) {
        QRect qr = toQRect(face.rect);
        if (face.known) {
            const QColor green(0x22, 0xc5, 0x5e);
            drawCornerBox(p, qr, green, 2);
            const QString label = face.name
                + QStringLiteral(" %1%").arg(face.similarity, 0, 'f', 0);
            drawLabel(p, qr, label, green);
        } else {
            const QColor yellow(0xea, 0xb3, 0x08);
            drawCornerBox(p, qr, yellow, 2);
            // 附相似度便于阈值标定（R1：相似度口径统一为 %）
            const QString label = face.similarity >= 0
                ? QStringLiteral("陌生人 %1%").arg(face.similarity, 0, 'f', 0)
                : QStringLiteral("陌生人");
            drawLabel(p, qr, label, yellow);
        }
    }

    // 入侵：红色全框加粗目标 + 顶部告警横幅（半透明色带，工业告警语义）
    if (intrusion.active) {
        p.setRenderHint(QPainter::Antialiasing, false);
        for (const cv::Rect& off : intrusion.offenders) {
            p.setPen(QPen(QColor(0xdc, 0x26, 0x26), 4));
            p.drawRect(toQRect(off));
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect band(0, 0, img.width(), 40);
        p.fillRect(band, QColor(0xdc, 0x26, 0x26, 200));
        QFont f = p.font();
        f.setPointSize(16);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0xff, 0xff, 0xff));
        p.drawText(band, Qt::AlignCenter, QStringLiteral("入 侵 告 警"));
    }

    // 诊断角标（FR-08）：预览fps · 分析Hz · 分析耗时 · 帧龄（延迟）
    if (hud.show) {
        const QString latency = hud.latencyMs >= 0
            ? QStringLiteral("%1ms").arg(hud.latencyMs)
            : QStringLiteral("—");
        const QString txt = QStringLiteral("预览 %1fps · 分析 %2Hz · %3ms · 延迟 %4")
                                .arg(hud.renderFps)
                                .arg(hud.analysisFps)
                                .arg(hud.processMs)
                                .arg(latency);
        QFont f = p.font();
        f.setPointSize(10);
        f.setBold(false);
        p.setFont(f);
        const QFontMetrics fm(p.font());
        const QRect tr = fm.boundingRect(txt);
        const QRect bg(img.width() - tr.width() - 22, 10, tr.width() + 18, tr.height() + 10);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(bg, 6, 6);
        p.setBrush(Qt::NoBrush);
        // 数值健康度配色：延迟可见即绿色（越界告警由事件系统承担）
        p.setPen(QColor(0x34, 0xd3, 0x99));
        p.drawText(bg, Qt::AlignCenter, txt);
    }

    p.end();
}
