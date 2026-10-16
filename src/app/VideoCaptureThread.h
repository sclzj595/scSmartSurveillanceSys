// VideoCaptureThread.h (文档6)
#ifndef VIDEOCAPTURETHREAD_H
#define VIDEOCAPTURETHREAD_H

#pragma execution_character_set("utf-8")

#include "dataConfig.h"
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <opencv2/opencv.hpp>

class VideoCaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit VideoCaptureThread(QObject *parent = nullptr);
    ~VideoCaptureThread();

    void startCapture(int cameraIndex = 0);
    void stopCapture();
    void pause();
    void resume();
    bool isRunning() const { return _isRunning; }

signals:
    void frameCaptured(cv::Mat frame);
    void fpsChanged(int fps);

protected:
    void run() override;

private:
    cv::VideoCapture _cap;
    QMutex _mutex;
    bool _isRunning = false;
    bool _isPaused = false;
    int _cameraIndex = 0;
    int _frameCount = 0;
    qint64 _startTime;
};

#endif  // VIDEOCAPTURETHREAD_H