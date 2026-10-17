// src/app/AnalysisThread.cpp
#include "pipeline/AnalysisThread.h"

#include <QElapsedTimer>
#include <QDebug>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "vision/FaceRecognition.h"
#include "vision/ObjectDetection.h"
#include "vision/IntrusionDetection.h"

namespace {
// 分析坐标 → 源帧坐标（降采样后检测框等比放大，超出边界时裁剪）
cv::Rect unscaleRect(const cv::Rect& r, const cv::Size& from, const cv::Size& to)
{
    const double sx = double(to.width)  / from.width;
    const double sy = double(to.height) / from.height;
    int x = cvRound(r.x * sx), y = cvRound(r.y * sy);
    int w = cvRound(r.width * sx), h = cvRound(r.height * sy);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w <= 0 || h <= 0) return r;                 // 退化保护：维持原值
    if (x + w > to.width)  w = to.width  - x;
    if (y + h > to.height) h = to.height - y;
    return (w > 0 && h > 0) ? cv::Rect(x, y, w, h) : r;
}
} // namespace

AnalysisThread::AnalysisThread(FrameBank* bank,
                               FaceRecognition* face,
                               ObjectDetection* object,
                               IntrusionDetection* intrusion,
                               QObject* parent)
    : QThread(parent)
    , _bank(bank)
    , _face(face)
    , _object(object)
    , _intrusion(intrusion)
{
    setObjectName(QStringLiteral("AnalysisThread"));
}

AnalysisThread::~AnalysisThread()
{
    shutdown();
    wait(2000);
}

void AnalysisThread::shutdown()
{
    _running = false;
}

void AnalysisThread::run()
{
    _running = true;
    int seenVersion = 0;
    qint64 lastStartMs = -1;
    QElapsedTimer clock;
    clock.start();
    int frames = 0;
    qint64 fpsWindowStart = 0;
    int analysisFps = 0;   // 持久化：非窗口收口帧也携带最近统计值（否则角标大部分时间显示 0Hz）
    int dropsWindow = 0;   // 本窗口内被覆盖丢弃的帧数（阶段02 帧覆盖率）
    int dropsPerSec = 0;

    qInfo() << "Analysis thread started";
    while (_running) {
        cv::Mat frame;
        int newVersion = seenVersion;
        qint64 storedMs = 0;
        if (!_bank->waitForNew(seenVersion, 100, frame, newVersion, &storedMs))
            continue;                       // 超时无新帧（暂停/断流），继续等待
        // 帧覆盖率统计：两窗之间被覆盖式单槽跳过的帧数（FR-01 丢帧保时延的可见化）
        if (newVersion > seenVersion + 1)
            dropsWindow += newVersion - seenVersion - 1;
        seenVersion = newVersion;
        if (frame.empty())
            continue;

        // 帧身份先行记录（阶段02）：即使本帧分析异常跳过，时间戳语义也已与采集对齐
        AnalysisResult result;
        result.captureTsMs = storedMs;
        result.frameVersion = newVersion;

        // 节流：识别频率上限 25Hz（预览渲染不受此限制）
        qint64 nowMs = clock.elapsed();
        if (lastStartMs > 0 && nowMs - lastStartMs < kMinIntervalMs)
            msleep(static_cast<ulong>(kMinIntervalMs - (nowMs - lastStartMs)));
        lastStartMs = clock.elapsed();

        QElapsedTimer cost;
        cost.start();

        // 分析降采样（阶段02）：HAAR/MOG2 耗时 ∝ 像素数，0.5x ≈ 1/4；
        // 结果坐标统一反缩放回源帧（渲染/禁区/存证坐标系不变）。
        // 抓拍快照取自缩放帧，但监控事件存证用全分辨率标注图，不受影响
        cv::Mat work;
        const bool scaled = _analysisScale < 0.999 && frame.cols >= 480;
        if (scaled)
            cv::resize(frame, work, cv::Size(), _analysisScale, _analysisScale, cv::INTER_AREA);
        else
            work = frame;                   // 浅拷贝共享引用计数，零 memcpy

        // 异常隔离：单帧算法异常（如 CV 断言）只跳过该帧，绝不杀死整个进程
        try {
            if (_face) {
                _face->analyze(work, result.faces);
                if (scaled)
                    for (FaceResult& f : result.faces)
                        f.rect = unscaleRect(f.rect, work.size(), frame.size());
            }
            if (_object) {
                _object->analyze(work, result.objects);
                if (scaled)
                    for (TrackedObjectResult& o : result.objects)
                        o.rect = unscaleRect(o.rect, work.size(), frame.size());
            }

            if (_intrusion && _intrusion->isZoneActive()) {
                std::vector<cv::Rect> faceRects;
                faceRects.reserve(result.faces.size());
                for (const FaceResult& f : result.faces)
                    faceRects.push_back(f.rect);   // 已是源帧坐标
                result.intrusion = _intrusion->analyze(faceRects);
            }
        } catch (const cv::Exception& e) {
            qWarning() << "Analysis CV exception, frame skipped:" << e.what();
            continue;
        } catch (const std::exception& e) {
            qWarning() << "Analysis exception, frame skipped:" << e.what();
            continue;
        }

        result.processMs = static_cast<int>(cost.elapsed());

        // 分析帧率/丢帧统计（约每秒刷新一次；每帧都携带最近值）
        ++frames;
        qint64 window = clock.elapsed() - fpsWindowStart;
        if (window >= 1000) {
            analysisFps = static_cast<int>(frames * 1000.0 / window + 0.5);
            dropsPerSec = dropsWindow;
            frames = 0;
            dropsWindow = 0;
            fpsWindowStart = clock.elapsed();
        }
        result.analysisFps = analysisFps;
        result.dropsPerSec = dropsPerSec;

        emit analysisReady(result);
    }
    qInfo() << "Analysis thread exited";
}
