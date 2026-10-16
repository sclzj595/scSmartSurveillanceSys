#ifndef OBJECTDETECTION_H
#define OBJECTDETECTION_H

#pragma execution_character_set("utf-8")

#include "dataConfig.h"
#include <QObject>
#include <opencv2/opencv.hpp>
#include <map>
#include <QMutex>
#include <QDateTime>
#include "AnalysisTypes.h"

class ObjectDetection : public QObject
{
    Q_OBJECT
public:
    explicit ObjectDetection(QObject *parent = nullptr);

    // 纯分析：更新背景模型与静止物体状态，输出结构化结果，不做任何绘制
    // 仅在分析线程调用
    void analyze(const cv::Mat &frame, std::vector<TrackedObjectResult>& out);

signals:
    void objectLeftDetected(const cv::Rect &position);

private:
    cv::Ptr<cv::BackgroundSubtractorMOG2> _pBackgroundSubtractor;
    std::map<int, std::pair<qint64, cv::Rect>> _stillObjects;
    int _nextObjectId = 0;
    QMutex _mutex;
    const double _timeThreshold = 10000; // 10秒阈值
};

#endif  // OBJECTDETECTION_H
