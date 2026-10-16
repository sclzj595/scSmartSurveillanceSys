#include "IntrusionDetection.h"
#include <QDebug>

IntrusionDetection::IntrusionDetection(QObject *parent) : QObject(parent) {}

IntrusionDetection::~IntrusionDetection() {}

void IntrusionDetection::setForbiddenZone(const cv::Rect& zone) {
    QMutexLocker locker(&_mutex);
    _forbiddenZone = zone;
    
    qInfo() << "Forbidden zone set: x=" << zone.x << "y=" << zone.y 
            << "w=" << zone.width << "h=" << zone.height;
}

IntrusionResult IntrusionDetection::analyze(const std::vector<cv::Rect>& targets) {
    QMutexLocker locker(&_mutex);
    IntrusionResult result;
    if (_forbiddenZone.empty()) return result;

    // 检测是否有物体进入禁区（超过30%面积进入视为入侵）
    for (const auto& obj : targets) {
        cv::Rect intersection = obj & _forbiddenZone;
        if (intersection.area() > obj.area() * 0.3) {
            result.active = true;
            result.offenders.push_back(obj);
        }
    }

    if (result.active)
        emit intrusionDetected();
    return result;
}