#include "VideoCaptureThread.h"

#include <QDateTime>
#include <QDebug>

VideoCaptureThread::VideoCaptureThread(QObject *parent) 
    : QThread(parent), _isRunning(false), _isPaused(false), _cameraIndex(0), 
      _frameCount(0), _startTime(0) 
{}

VideoCaptureThread::~VideoCaptureThread() {
    stopCapture();
}

void VideoCaptureThread::startCapture(int cameraIndex) {
    QMutexLocker locker(&_mutex);
    if (!_isRunning) {
        _cap.open(cameraIndex, cv::CAP_DSHOW); // 使用DirectShow API获取摄像头
        
        if (_cap.isOpened()) {
            _isRunning = true;
            _isPaused = false;
            _cameraIndex = cameraIndex;
            _frameCount = 0;
            _startTime = QDateTime::currentMSecsSinceEpoch();
            start();
            qInfo() << "Video capture started for camera" << cameraIndex;
        } else {
            qWarning() << "Failed to open camera" << cameraIndex;
        }
    }
}

void VideoCaptureThread::stopCapture() {
    if (_isRunning) {
        {
            QMutexLocker locker(&_mutex);
            _isRunning = false;
        }
        wait(500); // 等待线程退出
        if (_cap.isOpened()) {
            _cap.release();
            qInfo() << "Video capture stopped";
        }
    }
}

void VideoCaptureThread::pause() {
    QMutexLocker locker(&_mutex);
    _isPaused = true;
    qDebug() << "Video capture paused";
}

void VideoCaptureThread::resume() {
    QMutexLocker locker(&_mutex);
    _isPaused = false;
    qDebug() << "Video capture resumed";
}

void VideoCaptureThread::run() {
    cv::Mat frame;
    while (true) {
        {
            QMutexLocker locker(&_mutex);
            if (!_isRunning) break;
        }
        
        if (!_isPaused) {
            {
                QMutexLocker locker(&_mutex);
                _cap >> frame;
            }
            
            if (frame.empty()) {
                // 重连摄像头
                if (!_cap.isOpened()) {
                    _cap.open(_cameraIndex);
                }
                msleep(30);
                continue;
            }
            
            // 帧率统计
            _frameCount++;
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            qint64 elapsed = currentTime - _startTime;
            if (elapsed > 1000) { // 每1秒计算一次帧率
                int fps = (int)(_frameCount / (elapsed / 1000.0));
                _frameCount = 0;
                _startTime = currentTime;
                emit fpsChanged(fps);
                qDebug() << "Current FPS:" << fps;
            }
            
            // 发送捕获到的帧
            emit frameCaptured(frame.clone());
        }
        msleep(10); // 控制帧率
    }
}