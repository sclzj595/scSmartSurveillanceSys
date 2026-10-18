// VideoCaptureThread.h (文档6 / V0.6 重构)
#ifndef VIDEOCAPTURETHREAD_H
#define VIDEOCAPTURETHREAD_H

#pragma execution_character_set("utf-8")

#include "core/dataConfig.h"
#include <mvcore/FrameBank.h>
#include "pipeline/VideoSource.h"

// FrameBank 已提炼至套件共享库 mvcore（接口完全兼容，调用点零改动）
using mvcore::FrameBank;
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <memory>

// 采集线程：视频源取流 → 写入 FrameBank 最新帧槽（覆盖式，天然背压）
// 不再通过信号逐帧推送 GUI —— 消费端（渲染节拍器/分析线程）自行按需取帧，
// 从根上消除"GUI 消费慢 → 队列事件堆积 → 延迟累积"的问题（FR-01 / D1、D2）
//
// V0.6（D9 断流恢复）：线程持 IVideoSource 抽象，源只做单次 open/read；
// 断流/打开失败统一在本线程内 close → 指数退避（base→max）→ 重开，
// USB 与 RTSP 自动获得一致的恢复能力，重连状态经 sourceStatus 信号回 UI。
class VideoCaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit VideoCaptureThread(FrameBank* frameBank, QObject *parent = nullptr);
    ~VideoCaptureThread();

    // 异步启动：open 在 run() 内进行（RTSP 建连可达数秒，不能占 GUI 线程），
    // 打开失败会走重连退避并经 sourceStatus 上报；返回 false 表示线程未启动
    bool startCapture(const VideoSourceConfig& cfg);
    void stopCapture();
    bool switchSource(const VideoSourceConfig& cfg);   // 运行时换源：停→换→启
    void pause();
    void resume();
    bool isRunning() const { return _isRunning; }

signals:
    void fpsChanged(int fps);
    void sourceStatus(const QString& msg);   // 换源/断流/重连状态（GUI 日志栏）

protected:
    void run() override;

private:
    FrameBank* _bank = nullptr;
    VideoSourceConfig _config;
    std::unique_ptr<IVideoSource> _source;   // 仅 run()/startCapture 串行创建使用
    QMutex _mutex;                            // 保护 _isPaused 标志
    std::atomic<bool> _isRunning{false};
    bool _isPaused = false;
    int _frameCount = 0;
    qint64 _startTime;
    quint64 _lastSig = 0;   // 上一帧内容签名（重复帧去重，captureFps 口径=新帧/秒）
};

#endif  // VIDEOCAPTURETHREAD_H
