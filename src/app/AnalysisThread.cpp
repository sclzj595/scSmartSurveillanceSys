// src/app/AnalysisThread.cpp
#include "AnalysisThread.h"

#include <QElapsedTimer>
#include <QDebug>
#include "FaceRecognition.h"
#include "ObjectDetection.h"
#include "IntrusionDetection.h"

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

        AnalysisResult result;
        if (_face)      _face->analyze(frame, result.faces);
        if (_object)    _object->analyze(frame, result.objects);

        if (_intrusion && _intrusion->isZoneActive()) {
            std::vector<cv::Rect> faceRects;
            faceRects.reserve(result.faces.size());
            for (const FaceResult& f : result.faces)
                faceRects.push_back(f.rect);
            result.intrusion = _intrusion->analyze(faceRects);
        }

        result.processMs = static_cast<int>(cost.elapsed());

        // 分析帧率统计（约每秒刷新一次）
        ++frames;
        qint64 window = clock.elapsed() - fpsWindowStart;
        if (window >= 1000) {
            result.analysisFps = static_cast<int>(frames * 1000.0 / window + 0.5);
            frames = 0;
            fpsWindowStart = clock.elapsed();
        }

        emit analysisReady(result);
    }
    qInfo() << "Analysis thread exited";
}
