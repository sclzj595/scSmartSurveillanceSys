#include "VideoCaptureThread.h"

#include <QDateTime>
#include <QDebug>

VideoCaptureThread::VideoCaptureThread(FrameBank* frameBank, QObject *parent)
    : QThread(parent), _bank(frameBank), _isPaused(false), _cameraIndex(0),
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
        wait(2000);   // 设备释放移入 run() 退出路径，杜绝并发 release 竞态
        qInfo() << "Video capture stopped";
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
    while (_isRunning) {
        {
            QMutexLocker locker(&_mutex);
            if (!_isRunning) break;
            if (_isPaused) {
                // 暂停期间持续 grab 丢弃缓冲帧：恢复时直接从最新帧继续（FR-02 / 修复D8）
                if (_cap.isOpened())
                    _cap.grab();
                locker.unlock();
                msleep(10);
                continue;
            }
            _cap >> frame;
        }

        if (frame.empty()) {
            // 重连摄像头
            if (!_cap.isOpened()) {
                _cap.open(_cameraIndex, cv::CAP_DSHOW);
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
            qDebug() << "Capture FPS:" << fps;
        }

        // 写入最新帧槽：覆盖式，永不阻塞采集（FR-01 丢帧保时延）
        if (_bank)
            _bank->store(frame);
    }

    // 设备释放收尾：避免 stopCapture 与 run() 并发访问 _cap 导致崩溃
    {
        QMutexLocker locker(&_mutex);
        if (_cap.isOpened()) {
            _cap.release();
        }
    }
}
