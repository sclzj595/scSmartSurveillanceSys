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

    // 完整注册流程：灰度化→均衡化→检测→取最大人脸→入库训练
    // 可在任意线程调用（内部全程持 _mutex，与 analyze 串行化——
    // 同一 CascadeClassifier 并发 detectMultiScale 非线程安全，禁止无锁并发）
    RegisterStatus registerFromFrame(const cv::Mat& bgrFrame, const QString& name);

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
