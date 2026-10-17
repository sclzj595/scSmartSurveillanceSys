// src/include/AnalysisTypes.h
// 识别管线结构化结果定义（数据与呈现分离：分析线程产出，GUI 线程渲染）
#ifndef ANALYSISTYPES_H
#define ANALYSISTYPES_H

#include <QString>
#include <vector>
#include <QMetaType>
#include <opencv2/core.hpp>

// 人脸识别结果
struct FaceResult {
    cv::Rect rect;              // 源帧坐标系
    QString name;               // 命中档案名；未命中为空
    double similarity = -1.0;   // 0~100，越高越相似（R1 口径）；-1 = 无效（无样本/人脸过小）
    bool known = false;
};

// 遗留物/跟踪目标结果
struct TrackedObjectResult {
    cv::Rect rect;
    qint64 durationMs = 0;      // 已静止时长
    bool confirmedLeft = false; // 超过停留阈值，确认遗留
};

// 禁区入侵结果
struct IntrusionResult {
    bool active = false;
    std::vector<cv::Rect> offenders;   // 侵入禁区目标（源帧坐标系）
};

// 单帧分析汇总（跨线程队列信号传递）
struct AnalysisResult {
    std::vector<FaceResult> faces;
    std::vector<TrackedObjectResult> objects;
    IntrusionResult intrusion;
    qint64 captureTsMs = 0;     // 帧采集时刻（epoch ms，阶段02）：事件 ts 锚定视频瞬间，支撑事件↔画面精确回溯
    qint64 frameVersion = 0;    // 帧版本号（FrameBank 递增号）：帧的身份标识，跨线程追溯用
    int processMs = 0;          // 本帧分析耗时
    int analysisFps = 0;        // 分析频率（约每秒刷新一次）
    int dropsPerSec = 0;        // 帧覆盖率（阶段02）：每秒被覆盖式单槽丢弃、未经分析的帧数
};

Q_DECLARE_METATYPE(FaceResult)
Q_DECLARE_METATYPE(TrackedObjectResult)
Q_DECLARE_METATYPE(IntrusionResult)
Q_DECLARE_METATYPE(AnalysisResult)

#endif // ANALYSISTYPES_H
