// src/app/FrameBank.h
// 最新帧槽（Latest-Frame Slot）：线程间背压基元
//
// 设计决策（FR-01 丢帧保时延）：
//  - 生产者 store() 永不阻塞：新帧直接覆盖旧帧。识别/渲染消费不及时时
//    丢弃旧帧，只保留最新帧 —— 延迟有上界（单帧处理耗时），不会随运行时间累积。
//  - load() 返回 cv::Mat 引用计数浅拷贝（零 memcpy），代价 ~微秒。
//    注意：消费者若要修改像素（如叠加标注），必须自行 clone()，否则会与
//    其他持有同一数据的线程产生像素竞争。
//  - waitForNew() 供分析线程按"新帧到达"节奏调度（QWaitCondition），
//    避免忙轮询，也保证分析总是拿最新帧（天然跳帧）。
#ifndef FRAMEBANK_H
#define FRAMEBANK_H

#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QDateTime>
#include <opencv2/core.hpp>

class FrameBank
{
public:
    void store(cv::Mat frame)
    {
        {
            QMutexLocker locker(&_mutex);
            _frame = std::move(frame);
            _storedMs = QDateTime::currentMSecsSinceEpoch();
            ++_version;
        }
        _cond.wakeAll();
    }

    cv::Mat load() const
    {
        QMutexLocker locker(&_mutex);
        return _frame;
    }

    // 帧龄：当前帧从采集到现在经过的毫秒数（实时性指标，FR-01 / 5.2）
    qint64 ageMs() const
    {
        QMutexLocker locker(&_mutex);
        return _frame.empty() ? -1 : QDateTime::currentMSecsSinceEpoch() - _storedMs;
    }

    bool empty() const
    {
        QMutexLocker locker(&_mutex);
        return _frame.empty();
    }

    // 当前帧版本号（每次 store 递增）：渲染节拍按版本去重——
    // 采集帧率低于渲染上限时不重复绘制同一帧（阶段02 性能项）
    int version() const
    {
        QMutexLocker locker(&_mutex);
        return _version;
    }

    // 阻塞等待版本号超过 seenVersion 的新帧（最多 timeoutMs），取出最新帧。
    // 返回 false 表示超时无新帧（暂停/断流），调用方继续等待即可。
    bool waitForNew(int seenVersion, int timeoutMs, cv::Mat& out, int& newVersion)
    {
        QMutexLocker locker(&_mutex);
        if (_version == seenVersion)
            _cond.wait(&_mutex, static_cast<unsigned long>(timeoutMs));
        if (_version != seenVersion) {
            out = _frame;
            newVersion = _version;
            return true;
        }
        return false;
    }

private:
    mutable QMutex _mutex;
    QWaitCondition _cond;
    cv::Mat _frame;
    qint64 _storedMs = 0;
    int _version = 0;
};

#endif // FRAMEBANK_H
