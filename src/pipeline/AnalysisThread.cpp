// src/app/AnalysisThread.cpp
#include "pipeline/AnalysisThread.h"

#include <QElapsedTimer>
#include <QDebug>
#include <opencv2/core.hpp>
#include "vision/FaceRecognition.h"
#include "vision/ObjectDetection.h"
#include "vision/IntrusionDetection.h"

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

    qInfo() << "Analysis thread started";
    while (_running) {
        cv::Mat frame;
        int newVersion = seenVersion;
        if (!_bank->waitForNew(seenVersion, 100, frame, newVersion))
            continue;                       // 超时无新帧（暂停/断流），继续等待
        seenVersion = newVersion;
        if (frame.empty())
            continue;

        // 节流：识别频率上限 25Hz（预览渲染不受此限制）
        qint64 nowMs = clock.elapsed();
        if (lastStartMs > 0 && nowMs - lastStartMs < kMinIntervalMs)
            msleep(static_cast<ulong>(kMinIntervalMs - (nowMs - lastStartMs)));
        lastStartMs = clock.elapsed();

        QElapsedTimer cost;
        cost.start();

        // 异常隔离：单帧算法异常（如 CV 断言）只跳过该帧，绝不杀死整个进程
        AnalysisResult result;
        try {
            if (_face)      _face->analyze(frame, result.faces);
            if (_object)    _object->analyze(frame, result.objects);

            if (_intrusion && _intrusion->isZoneActive()) {
                std::vector<cv::Rect> faceRects;
                faceRects.reserve(result.faces.size());
                for (const FaceResult& f : result.faces)
                    faceRects.push_back(f.rect);
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

        // 分析帧率统计（约每秒刷新一次；每帧都携带最近值）
        ++frames;
        qint64 window = clock.elapsed() - fpsWindowStart;
        if (window >= 1000) {
            analysisFps = static_cast<int>(frames * 1000.0 / window + 0.5);
            frames = 0;
            fpsWindowStart = clock.elapsed();
        }
        result.analysisFps = analysisFps;

        emit analysisReady(result);
    }
    qInfo() << "Analysis thread exited";
}
