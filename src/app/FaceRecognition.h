// FaceRecognition.h (文档9)
#ifndef FACERECOGNITION_H
#define FACERECOGNITION_H

#pragma execution_character_set("utf-8")

#include <QObject>
#include <QMutex>
#include <opencv2/opencv.hpp>
#include <vector>
#include <map>
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
    // 可在任意线程调用（内部持 _mutex，与 analyze 串行化——
    // 同一 CascadeClassifier 并发 detectMultiScale 非线程安全）
    struct RegisterDetection {
        RegisterStatus status = RegisterStatus::NoFace;
        cv::Mat face;          // Ok 时有效：人脸灰度 ROI
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
    class LBPHRecognizer {
    public:
        LBPHRecognizer();
        cv::Mat computeLBP(const cv::Mat& src) const;
        void train(const std::vector<cv::Mat>& images, const std::vector<int>& labels);
        void predict(const cv::Mat& src, int& label, double& confidence) const;
    private:
        std::map<int, std::vector<cv::Mat>> _model;
    };

    // 以下方法都必须在持有 _mutex 时调用
    void detectFaces(cv::Mat &gray, std::vector<cv::Rect> &faces);
    void registerFaceLocked(const cv::Mat& grayFace, const QString& name);
    QString suggestedNameLocked() const;

    cv::CascadeClassifier _faceCascade;
    LBPHRecognizer _recognizer;
    std::map<int, QString> _labelMap;
    int _nextLabel = 0;
    cv::Size _faceSize = cv::Size(100, 100);
    bool _enabled = false;   // 与界面默认"人脸识别: 关闭"一致
    QMutex _mutex;           // 保护分类器/人脸库（分析线程 vs 注册任务）

    std::vector<cv::Mat> _allSamples;
    std::vector<int> _allLabels;
};

#endif // FACERECOGNITION_H
