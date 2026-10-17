#ifndef OBJECTDETECTION_H
#define OBJECTDETECTION_H

#pragma execution_character_set("utf-8")

#include "core/dataConfig.h"
#include <QObject>
#include <opencv2/opencv.hpp>
#include <map>
#include <QMutex>
#include <QDateTime>
#include "core/AnalysisTypes.h"

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
    // 新目标确认（防噪点刷屏）：候选连续 N 帧出现才登记为正式目标。
    // 无确认时 AE 微调/光照突变的单帧噪块会反复 "New object detected" 刷日志
    std::map<int, std::pair<int, cv::Rect>> _candidateObjects;   // id → (连续帧数, 框)
    const int _confirmFrames = 3;        // 连续 3 帧确认
    const double _maxAreaRatio = 0.6;    // 轮廓超帧面积 60% → 整帧光照突变误检，丢弃
};

#endif  // OBJECTDETECTION_H
