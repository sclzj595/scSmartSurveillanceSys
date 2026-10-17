#include "vision/ObjectDetection.h"
#include <QDebug>
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp> // 确保包含video模块头文件

ObjectDetection::ObjectDetection(QObject *parent) : QObject(parent)
{
    // 创建MOG2背景减除器（具体类型）
    _pBackgroundSubtractor = cv::createBackgroundSubtractorMOG2();
    
    // 直接设置参数（无需set方法）
    _pBackgroundSubtractor->setHistory(500);
    _pBackgroundSubtractor->setDetectShadows(false);
    
    qInfo() << "Background subtractor initialized";
}

void ObjectDetection::analyze(const cv::Mat &frame, std::vector<TrackedObjectResult>& out)
{
    out.clear();
    if (frame.empty()) return;

    cv::Mat fgMask;
    _pBackgroundSubtractor->apply(frame, fgMask);

    // 形态学操作去除噪声
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(fgMask, fgMask, cv::MORPH_OPEN, kernel);
    cv::dilate(fgMask, fgMask, kernel);

    // 查找轮廓
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(fgMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    QMutexLocker locker(&_mutex);
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    // 更新现有静止物体状态
    for (auto it = _stillObjects.begin(); it != _stillObjects.end();) {
        bool found = false;
        for (const auto& contour : contours) {
            double area = cv::contourArea(contour);
            if (area < 500) continue; // 忽略小物体
                
            cv::Rect rect = cv::boundingRect(contour);
            cv::Rect intersection = rect & it->second.second;
            
            if (intersection.area() > rect.area() * 0.7) { // 70%重叠视为同一物体
                it->second.second = rect; // 更新位置
                found = true;
                break;
            }
        }
        
        if (found) {
            ++it;
        } else {
            // 物体已经消失，检查停留时间是否超时
            if (currentTime - it->second.first > _timeThreshold) {
                emit objectLeftDetected(it->second.second);
                qInfo() << "Object left detected after" 
                        << (currentTime - it->second.first) / 1000.0 << "seconds";
            }
            it = _stillObjects.erase(it);
        }
    }
    
    // 检测新物体
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < 500) continue; // 忽略小物体
            
        cv::Rect rect = cv::boundingRect(contour);
        bool isNewObject = true;
        
        // 检查是否已经存在
        for (const auto& obj : _stillObjects) {
            cv::Rect existingRect = obj.second.second;
            cv::Rect intersection = rect & existingRect;
            
            if (intersection.area() > rect.area() * 0.3) {
                isNewObject = false;
                break;
            }
        }
        
        if (isNewObject) {
            _stillObjects[_nextObjectId] = std::make_pair(currentTime, rect);
            qInfo() << "New object detected:" << rect.x << "," << rect.y
                    << rect.width << "x" << rect.height;
            _nextObjectId++;
        }
    }
    
    // 输出结构化结果（绘制由 GUI 线程 OverlayRenderer 承担）
    for (const auto& obj : _stillObjects) {
        TrackedObjectResult r;
        r.rect = obj.second.second;
        r.durationMs = currentTime - obj.second.first;
        r.confirmedLeft = r.durationMs > _timeThreshold;
        out.push_back(r);
    }
}