#include "pipeline/VideoCaptureThread.h"

#include <QDateTime>
#include <QDebug>
#include <cstring>

namespace {
// 帧内容签名：FNV-1a 对 32 处分段采样（避免整帧 memcmp；分段跨全帧防
// 只比头部漏检）。用于重复帧去重——部分 OpenCV 后端（FFmpeg/RTSP）在无新
// 数据时会重复返回上一帧而非阻塞，不去重会让 capture_fps 虚高（RTSP 合成
// 流实测 801/s）且 FrameBank 版本号空转、分析线程重复跑同一帧
inline quint64 frameSignature(const cv::Mat& m)
{
    const uchar* d = m.data;
    const size_t n = m.total() * m.elemSize();
    if (!d || n == 0) return 0;
    quint64 h = 1469598103934665603ull;
    for (int i = 0; i < 32; ++i) {
        const size_t off = n * size_t(i) / 32;
        quint64 chunk = 0;
        memcpy(&chunk, d + off, qMin<size_t>(8, n - off));
        h = (h ^ chunk) * 1099511628211ull;
    }
    return h;
}
}  // namespace

VideoCaptureThread::VideoCaptureThread(FrameBank* frameBank, QObject *parent)
    : QThread(parent), _bank(frameBank), _isPaused(false),
      _frameCount(0), _startTime(0)
{}

VideoCaptureThread::~VideoCaptureThread() {
    stopCapture();
}

bool VideoCaptureThread::startCapture(const VideoSourceConfig& cfg) {
    QMutexLocker locker(&_mutex);
    if (_isRunning)
        return false;
    _config = cfg;
    _isPaused = false;
    _frameCount = 0;
    _lastSig = 0;   // 换源重置：首帧必为新帧
    _startTime = QDateTime::currentMSecsSinceEpoch();
    _isRunning = true;
    start();
    qInfo() << "Video capture requested, source type =" << int(cfg.type);
    return true;
}

void VideoCaptureThread::stopCapture() {
    if (_isRunning) {
        _isRunning = false;
        // run() 的 read 受 RTSP readTimeoutMs(3s) 上界约束，join 需覆盖
        // "一次 read + 收尾释放"，否则旧线程未退出就换源会双线程并存
        if (!wait(8000))
            qWarning() << "Capture thread did not exit within 8s";
        qInfo() << "Video capture stopped";
    }
}

bool VideoCaptureThread::switchSource(const VideoSourceConfig& cfg) {
    if (cfg == _config && _isRunning)
        return true;          // 同源重复切换：幂等返回，避免无谓断流
    stopCapture();
    return startCapture(cfg);
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
    const QString srcDesc = [] (const VideoSourceConfig& c) {
        // describe() 需要实例；此处先给配置级描述，open 成功后以实例描述为准
        return c.type == SourceType::RtspStream
            ? QStringLiteral("RTSP 流") : QStringLiteral("USB 摄像头 %1").arg(c.usbIndex);
    } (_config);

    _source = createVideoSource(_config);
    if (!_source) {
        emit sourceStatus(QStringLiteral("视频源配置无效，采集未启动"));
        _isRunning = false;
        return;
    }

    emit sourceStatus(QStringLiteral("正在打开 %1 …").arg(srcDesc));
    if (_source->open())
        emit sourceStatus(QStringLiteral("已接入 %1").arg(srcDesc));
    else
        emit sourceStatus(QStringLiteral("%1 打开失败，自动重连中…").arg(srcDesc));

    cv::Mat frame;
    int backoff = _config.reconnectBaseMs;

    while (_isRunning) {
        {
            QMutexLocker locker(&_mutex);
            if (!_isRunning) break;
            if (_isPaused) {
                // 暂停期间持续取帧丢弃：恢复时直接从最新帧继续（FR-02 / 修复D8）；
                // RTSP 下还兼具清空网络缓冲、防止服务端堆积的作用
                cv::Mat discard;
                if (_source) _source->read(discard);
                locker.unlock();
                msleep(10);
                continue;
            }
        }

        const bool got = _source->read(frame) && !frame.empty();
        if (!got) {
            // ---- D9 断流恢复：close → 指数退避 → 重开（USB 拔插 / RTSP 断网统一路径）----
            emit sourceStatus(QStringLiteral(
                "%1 信号丢失，%2 ms 后重连…").arg(srcDesc).arg(backoff));
            qWarning() << "Capture source lost, reconnect backoff" << backoff << "ms";
            _source->close();

            // 退避等待拆成小片，保证 stopCapture 能及时汇合
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + backoff;
            while (_isRunning && QDateTime::currentMSecsSinceEpoch() < deadline)
                msleep(20);
            if (!_isRunning) break;

            if (_source->open()) {
                backoff = _config.reconnectBaseMs;
                emit sourceStatus(QStringLiteral("%1 重连成功").arg(srcDesc));
                qInfo() << "Capture source reconnected";
            } else {
                backoff = qMin(backoff * 2, _config.reconnectMaxMs);
            }
            continue;
        }

        // 重复帧去重：内容签名与上一帧相同 → 非新帧（FFmpeg/RTSP 后端无新数据
        // 时会重复回吐上一帧而非阻塞）。不计新帧、不投递分析；但帧率窗口照常
        // 结算——"源冻结回吐"如实表现为 capture_fps=0，而非此前 801/s 虚高
        const quint64 sig = frameSignature(frame);
        if (sig == _lastSig) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - _startTime > 1000) {
                emit fpsChanged(_frameCount);   // 窗口内全为重复帧 → 0
                _frameCount = 0;
                _startTime = now;
            }
            continue;
        }
        _lastSig = sig;

        // 帧率统计（口径：新帧/秒 = 成功解码且内容变化）
        _frameCount++;
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        qint64 elapsed = currentTime - _startTime;
        if (elapsed > 1000) { // 每1秒计算一次帧率
            int fps = (int)(_frameCount / (elapsed / 1000.0));
            _frameCount = 0;
            _startTime = currentTime;
            emit fpsChanged(fps);
            qDebug() << "Capture FPS (new frames):" << fps;
        }

        // 写入最新帧槽：覆盖式，永不阻塞采集（FR-01 丢帧保时延）
        if (_bank)
            _bank->store(frame);
    }

    // 设备释放收尾：stopCapture 不碰源对象，全部释放集中在此（避免竞态）
    if (_source) {
        _source->close();
        _source.reset();
    }
}
