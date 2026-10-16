// VideoCaptureThread.h (文档6)
#ifndef VIDEOCAPTURETHREAD_H
#define VIDEOCAPTURETHREAD_H

#pragma execution_character_set("utf-8")

#include "dataConfig.h"
#include "FrameBank.h"
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <opencv2/opencv.hpp>

// 采集线程：摄像头取流 → 写入 FrameBank 最新帧槽（覆盖式，天然背压）
// 不再通过信号逐帧推送 GUI —— 消费端（渲染节拍器/分析线程）自行按需取帧，
// 从根上消除"GUI 消费慢 → 队列事件堆积 → 延迟累积"的问题（FR-01 / D1、D2）
class VideoCaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit VideoCaptureThread(FrameBank* frameBank, QObject *parent = nullptr);
    ~VideoCaptureThread();

    void startCapture(int cameraIndex = 0);
    void stopCapture();
    void pause();
    void resume();
    bool isRunning() const { return _isRunning; }

signals:
    void fpsChanged(int fps);

protected:
    void run() override;

private:
    FrameBank* _bank = nullptr;
    cv::VideoCapture _cap;
    QMutex _mutex;                  // 保护 _cap 与 _isRunning 写路径
    std::atomic<bool> _isRunning{false};
    bool _isPaused = false;
    int _cameraIndex = 0;
    int _frameCount = 0;
    qint64 _startTime;
};

#endif  // VIDEOCAPTURETHREAD_H
