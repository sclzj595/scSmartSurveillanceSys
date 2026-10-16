// IntrusionDetection.h (文档11)
#ifndef INTRUSIONDETECTION_H
#define INTRUSIONDETECTION_H

#pragma execution_character_set("utf-8")

#include "dataConfig.h"
#include <QObject>
#include <opencv2/opencv.hpp>
#include <vector>
#include <QMutex>

class IntrusionDetection : public QObject
{
    Q_OBJECT
    
public:
    explicit IntrusionDetection(QObject *parent = nullptr);
    ~IntrusionDetection();

    void processFrame(cv::Mat &frame, const std::vector<cv::Rect>& detectedObjects);
    void setForbiddenZone(const cv::Rect& zone);
    void clearForbiddenZone() { 
        QMutexLocker locker(&_mutex);
        _forbiddenZone = cv::Rect(); 
    }
    bool isZoneActive() const { return !_forbiddenZone.empty(); }

signals:
    void intrusionDetected();

private:
    cv::Rect _forbiddenZone;
    QMutex _mutex;
};

#endif  // INTRUSIONDETECTION_H