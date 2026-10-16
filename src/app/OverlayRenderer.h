// src/app/OverlayRenderer.h
// 画面标注渲染器（FR-08 标注规范 / R1 相似度口径 / R3 坐标系）
//
// 职责：把 AnalysisResult 的结构化结果绘制到 QImage 上。仅在 GUI 渲染节拍内
// 调用（QPainter 绘制，支持中文文本），分析线程不做任何绘制。
#ifndef OVERLAYRENDERER_H
#define OVERLAYRENDERER_H

#include <QImage>
#include <QPainter>
#include <QString>
#include "AnalysisTypes.h"

class OverlayRenderer
{
public:
    struct Hud {
        int renderFps = 0;    // 实际预览渲染帧率
        int analysisFps = 0;  // 识别管线频率
        int processMs = 0;    // 最近一帧分析耗时
        bool show = true;     // 诊断角标开关（FR-09）
    };

    static void paint(QImage& img,
                      const std::vector<FaceResult>& faces,
                      const std::vector<TrackedObjectResult>& objects,
                      const IntrusionResult& intrusion,
                      const cv::Rect& zone,
                      const Hud& hud);

private:
    static QRect toQRect(const cv::Rect& r);
    static void drawLabel(QPainter& p, const QRect& box,
                          const QString& text, const QColor& color);
};

#endif // OVERLAYRENDERER_H
