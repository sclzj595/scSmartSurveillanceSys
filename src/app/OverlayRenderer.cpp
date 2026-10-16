// src/app/OverlayRenderer.cpp
#include "OverlayRenderer.h"

QRect OverlayRenderer::toQRect(const cv::Rect& r)
{
    return QRect(r.x, r.y, r.width, r.height);
}

// 框上方的带底色文字标签（中文由 QPainter 绘制，修复 OpenCV putText 乱码问题）
void OverlayRenderer::drawLabel(QPainter& p, const QRect& box,
                                const QString& text, const QColor& color)
{
    if (text.isEmpty())
        return;
    QFont f = p.font();
    f.setPointSize(11);
    f.setBold(true);
    p.setFont(f);
    QFontMetrics fm(p.font());
    QRect tr = fm.boundingRect(text);
    int ty = box.top() - tr.height() - 6;
    if (ty < 2)
        ty = box.top() + 4;                 // 顶部放不下 → 移到框内
    QRect bg(box.left(), ty, tr.width() + 10, tr.height() + 4);
    p.fillRect(bg, QColor(0, 0, 0, 140));
    p.setPen(color);
    p.drawText(bg, Qt::AlignCenter, text);
}

void OverlayRenderer::paint(QImage& img,
                            const std::vector<FaceResult>& faces,
                            const std::vector<TrackedObjectResult>& objects,
                            const IntrusionResult& intrusion,
                            const cv::Rect& zone,
                            const Hud& hud)
{
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);

    // 禁区：红色虚线框（Qt::DashLine 原生支持）
    if (!zone.empty()) {
        const QColor zoneColor(0xef, 0x44, 0x44);
        p.setPen(QPen(zoneColor, 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(toQRect(zone));
        drawLabel(p, toQRect(zone), QStringLiteral("禁区"), zoneColor);
    }

    // 遗留物 / 跟踪目标：确认遗留 → 琥珀色加粗 + 标签；跟踪中 → 低调灰细框（FR-05 标注纪律）
    for (const TrackedObjectResult& obj : objects) {
        QRect qr = toQRect(obj.rect);
        if (obj.confirmedLeft) {
            const QColor amber(0xf5, 0x9e, 0x0b);
            p.setPen(QPen(amber, 3));
            p.drawRect(qr);
            const qint64 minutes = qMax<qint64>(1, obj.durationMs / 60000);
            drawLabel(p, qr, QStringLiteral("遗留物 %1分钟").arg(minutes), amber);
        } else {
            p.setPen(QPen(QColor(0x9c, 0xa3, 0xaf), 1));
            p.drawRect(qr);
        }
    }

    // 人脸：命中 → 绿框 + 档案名 + 相似度%；未命中 → 黄框 + 陌生人（FR-08）
    for (const FaceResult& face : faces) {
        QRect qr = toQRect(face.rect);
        if (face.known) {
            const QColor green(0x22, 0xc5, 0x5e);
            p.setPen(QPen(green, 2));
            p.drawRect(qr);
            const QString label = face.name
                + QStringLiteral(" %1%").arg(face.similarity, 0, 'f', 0);
            drawLabel(p, qr, label, green);
        } else {
            const QColor yellow(0xea, 0xb3, 0x08);
            p.setPen(QPen(yellow, 2));
            p.drawRect(qr);
            drawLabel(p, qr, QStringLiteral("陌生人"), yellow);
        }
    }

    // 入侵：红色加粗目标框 + 顶部告警横幅
    if (intrusion.active) {
        for (const cv::Rect& off : intrusion.offenders) {
            p.setPen(QPen(QColor(0xdc, 0x26, 0x26), 4));
            p.drawRect(toQRect(off));
        }
        QFont f = p.font();
        f.setPointSize(18);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0xdc, 0x26, 0x26));
        p.drawText(QRect(20, 14, img.width() - 40, 40), Qt::AlignLeft,
                   QStringLiteral("入侵告警!"));
    }

    // 诊断角标（FR-08）：预览fps · 分析Hz · 分析耗时
    if (hud.show) {
        const QString txt = QStringLiteral("预览 %1fps · 分析 %2Hz · %3ms")
                                .arg(hud.renderFps)
                                .arg(hud.analysisFps)
                                .arg(hud.processMs);
        QFont f = p.font();
        f.setPointSize(10);
        f.setBold(false);
        p.setFont(f);
        QFontMetrics fm(p.font());
        QRect tr = fm.boundingRect(txt);
        QRect bg(img.width() - tr.width() - 18, 8, tr.width() + 12, tr.height() + 8);
        p.fillRect(bg, QColor(0, 0, 0, 150));
        p.setPen(QColor(0x34, 0xd3, 0x99));
        p.drawText(bg, Qt::AlignCenter, txt);
    }

    p.end();
}
