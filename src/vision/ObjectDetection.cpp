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
    
    // 检测新物体（两级防误报）：
    // 1) 整帧过滤：轮廓面积超帧 60% = 光照突变/AE 调整导致整帧前景，不是真目标
    // 2) 连续帧确认：候选目标连续 3 帧出现才登记，单帧噪点直接丢弃
    const double frameArea = double(frame.cols) * frame.rows;
    std::vector<std::pair<int, cv::Rect>> confirmedThisFrame;   // 本帧确认的候选
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < 500) continue; // 忽略小物体
        cv::Rect rect = cv::boundingRect(contour);
        if (area > frameArea * _maxAreaRatio) continue;         // 整帧误检，丢弃

        bool isNewObject = true;
        // 检查是否已经存在（正式目标）
        for (const auto& obj : _stillObjects) {
            cv::Rect existingRect = obj.second.second;
            cv::Rect intersection = rect & existingRect;
            if (intersection.area() > rect.area() * 0.3) {
                isNewObject = false;
                break;
            }
        }
        if (!isNewObject) continue;

        // 候选确认：与已有候选框重叠则累加计数，达到阈值转正
        bool candidateMatched = false;
        for (auto& cand : _candidateObjects) {
            cv::Rect inter = rect & cand.second.second;
            if (inter.area() > rect.area() * 0.3) {
                ++cand.second.first;                // 连续出现计数
                cand.second.second = rect;
                if (cand.second.first >= _confirmFrames)
                    confirmedThisFrame.emplace_back(cand.first, rect);
                candidateMatched = true;
                break;
            }
        }
        if (candidateMatched) continue;
        _candidateObjects[_nextObjectId++] = std::make_pair(1, rect);
    }

    // 候选转正：登记为正式目标（确认帧统一在此打日志，避免中间态刷屏）
    for (const auto& [id, rect] : confirmedThisFrame) {
        _stillObjects[id] = std::make_pair(currentTime, rect);
        _candidateObjects.erase(id);   // 转正即出列，防止后续帧重复转正
        qInfo() << "New object detected:" << rect.x << "," << rect.y
                << rect.width << "x" << rect.height;
    }
    // 落选候选清理：不再出现的候选移除，防止 map 无限增长
    for (auto it = _candidateObjects.begin(); it != _candidateObjects.end();) {
        bool stillVisible = false;
        for (const auto& contour : contours) {
            const double area = cv::contourArea(contour);
            if (area < 500 || area > frameArea * _maxAreaRatio) continue;
            cv::Rect rect = cv::boundingRect(contour);
            cv::Rect inter = rect & it->second.second;
            if (inter.area() > rect.area() * 0.3) { stillVisible = true; break; }
        }
        if (stillVisible)
            ++it;
        else
            it = _candidateObjects.erase(it);
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