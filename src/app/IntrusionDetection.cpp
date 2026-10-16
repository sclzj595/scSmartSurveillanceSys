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

void IntrusionDetection::processFrame(cv::Mat &frame, const std::vector<cv::Rect>& detectedObjects) {
    QMutexLocker locker(&_mutex);
    if (_forbiddenZone.empty()) return;
    
    // 绘制禁区矩形（红色）
    cv::rectangle(frame, _forbiddenZone, cv::Scalar(0, 0, 255), 2);
    
    // 检测是否有物体进入禁区
    bool intrusionOccurred = false;  // 修改1：重命名局部变量避免冲突
    for (const auto& obj : detectedObjects) {
        cv::Rect intersection = obj & _forbiddenZone;
        if (intersection.area() > obj.area() * 0.3) { // 超过30%面积进入禁区
            intrusionOccurred = true;  // 修改2：使用新变量名
            
            // 标记入侵物体（蓝色）
            cv::rectangle(frame, obj, cv::Scalar(255, 0, 0), 3);
        }
    }
    
    if (intrusionOccurred) {  // 修改3：统一使用新变量名
        // 绘制警告信息
        cv::putText(frame, "INTRUSION ALERT!", cv::Point(50, 50), 
                    cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(0, 0, 255), 3);
        
        // 触发警报信号（修复冲突）
        emit intrusionDetected();  // 现在编译器能正确识别信号
    }
}