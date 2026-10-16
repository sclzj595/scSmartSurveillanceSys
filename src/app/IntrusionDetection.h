// IntrusionDetection.h (文档11)
#ifndef INTRUSIONDETECTION_H
#define INTRUSIONDETECTION_H

#pragma execution_character_set("utf-8")

#include "dataConfig.h"
#include <QObject>
#include <opencv2/opencv.hpp>
#include <vector>
#include <QMutex>
#include "AnalysisTypes.h"

class IntrusionDetection : public QObject
{
    Q_OBJECT

public:
    explicit IntrusionDetection(QObject *parent = nullptr);
    ~IntrusionDetection();

    // 纯分析：判定目标是否侵入禁区，输出结构化结果，不做任何绘制
    // 仅在分析线程调用
    IntrusionResult analyze(const std::vector<cv::Rect>& targets);

    void setForbiddenZone(const cv::Rect& zone);
    void clearForbiddenZone() {
        QMutexLocker locker(&_mutex);
        _forbiddenZone = cv::Rect();
    }
    bool isZoneActive() const {
        QMutexLocker locker(&_mutex);
        return !_forbiddenZone.empty();
    }
    cv::Rect zone() const {   // 渲染端绘制禁区用（R3：源帧坐标系）
        QMutexLocker locker(&_mutex);
        return _forbiddenZone;
    }

signals:
    void intrusionDetected();

private:
    cv::Rect _forbiddenZone;
    mutable QMutex _mutex;   // const 访问器（zone/isZoneActive）也需加锁
};

#endif  // INTRUSIONDETECTION_H
