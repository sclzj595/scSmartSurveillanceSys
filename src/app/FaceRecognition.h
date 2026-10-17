// FaceRecognition.h
// 业务适配器：将 falgo::FaceAlgorithm 独立算法库（FaceAlgorithm.dll）接入监控业务
//
// 组件化边界（PRD v2.1 第 4 章）：
//   - 算法本体（检测/识别/预处理/抓拍/模型存取）在 ../FaceAlgorithm 独立 DLL，
//     与闸机项目共用同一份算法，改 LBPH/换检测模型两边同步生效
//   - 本类只做业务翻译：label→姓名映射、建议名编号、开关控制、互斥编排
#ifndef FACERECOGNITION_H
#define FACERECOGNITION_H

#pragma execution_character_set("utf-8")

#include <QObject>
#include <QMutex>
#include <opencv2/opencv.hpp>
#include <vector>
#include <map>
#include <facealgorithm/FaceAlgorithm.h>
#include "dataConfig.h"
#include "AnalysisTypes.h"

class FaceRecognition : public QObject
{
    Q_OBJECT

public:
    enum class RegisterStatus {
        Ok,             // 注册成功
        NoFace,         // 未检测到人脸
        FaceTooSmall    // 人脸过小（<60px），拒绝注册
    };

    explicit FaceRecognition(QObject *parent = nullptr);
    ~FaceRecognition();

    // 纯分析：输出结构化人脸结果，不做任何绘制（绘制由 GUI 线程 OverlayRenderer 承担）
    // 仅在分析线程调用；内部持 _mutex
    void analyze(const cv::Mat& frame, std::vector<FaceResult>& out);

    // 两段式注册 · 第一步：检测并提取最大人脸（不入库）
    // 可在任意线程调用（内部持 _mutex，与 analyze 串行化）
    struct RegisterDetection {
        RegisterStatus status = RegisterStatus::NoFace;
        cv::Mat face;          // Ok 时有效：归一化灰度人脸 100x100
        QString suggestedName; // 弹窗建议名（"用户N"，自动跳过已占用）
    };
    RegisterDetection detectFaceForRegister(const cv::Mat& bgrFrame);

    // 两段式注册 · 第二步：将已提取人脸入库到指定姓名
    // 同名视为补充样本（同一人多次注册提高识别率）；与 analyze 互斥
    bool commitRegister(const cv::Mat& grayFace, const QString& name);

    void setEnabled(bool enabled) { _enabled = enabled; }

signals:
    void faceRecognized(QString name, double similarity);

private:
    // 前置条件：_mutex 已持有
    QString suggestedNameLocked() const;

    falgo::FaceAlgorithm _algo;          // 可插拔算法库实例（内部自互斥）
    std::map<int, QString> _labelMap;    // 业务侧：label → 姓名（算法库不知姓名概念）
    bool _enabled = false;               // 与界面默认"人脸识别: 关闭"一致
    QMutex _mutex;                       // 保护 labelMap 与算法调用的业务一致性
};

#endif // FACERECOGNITION_H
