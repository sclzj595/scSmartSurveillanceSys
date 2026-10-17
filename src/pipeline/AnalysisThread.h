// src/app/AnalysisThread.h
// 识别管线线程（Active Object 模式）
//
// 以"最新可用帧"为输入，串行执行 人脸识别 → 遗留物检测 → 禁区入侵判定，
// 产出结构化结果 AnalysisResult（不承担任何绘制职责，绘制在 GUI 线程的
// OverlayRenderer 中完成）。
//
// 速率自适应（FR-01）：
//  - 处理耗时超过采集周期时，FrameBank 自动跳帧到最新帧（丢帧保时延）；
//  - 识别频率上限 25Hz 节流，节省 CPU，预览渲染不受影响（满帧率独立运行）。
#ifndef ANALYSISTHREAD_H
#define ANALYSISTHREAD_H

#include <QThread>
#include <atomic>
#include <mvcore/FrameBank.h>
#include "core/AnalysisTypes.h"

// FrameBank 已提炼至套件共享库 mvcore（接口完全兼容，调用点零改动）
using mvcore::FrameBank;

class FaceRecognition;
class ObjectDetection;
class IntrusionDetection;

class AnalysisThread : public QThread
{
    Q_OBJECT
public:
    explicit AnalysisThread(FrameBank* bank,
                            FaceRecognition* face,
                            ObjectDetection* object,
                            IntrusionDetection* intrusion,
                            QObject* parent = nullptr);
    ~AnalysisThread() override;

    void shutdown();   // 线程安全：置位退出标志

    // 分析降采样（阶段02 性能项）：HAAR/MOG2 计算量 ∝ 像素数，0.5x ≈ 1/4 耗时。
    // start() 前调用一次（无并发）；检测结果坐标统一反缩放回源帧坐标系
    void setAnalysisScale(double s) { _analysisScale = qBound(0.25, s, 1.0); }

signals:
    void analysisReady(const AnalysisResult& result);

protected:
    void run() override;

private:
    static const int kMinIntervalMs = 40;  // 识别频率上限 25Hz

    FrameBank* _bank;
    FaceRecognition* _face;
    ObjectDetection* _object;
    IntrusionDetection* _intrusion;
    std::atomic<bool> _running{false};
    double _analysisScale = 1.0;           // 分析分辨率缩放（1.0 = 全分辨率）
};

#endif // ANALYSISTHREAD_H
