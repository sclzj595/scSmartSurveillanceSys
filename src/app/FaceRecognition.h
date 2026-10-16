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
    explicit FaceRecognition(QObject *parent = nullptr);
    ~FaceRecognition();

    // 纯分析：输出结构化人脸结果，不做任何绘制（绘制由 GUI 线程 OverlayRenderer 承担）
    // 仅在分析线程调用；与 registerFace 通过 _mutex 互斥
    void analyze(const cv::Mat& frame, std::vector<FaceResult>& out);

    void registerFace(const cv::Mat& face, const QString& name);
    void detectFaces(cv::Mat &gray, std::vector<cv::Rect> &faces);  // 注意：调用方负责先做直方图均衡化
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

    cv::CascadeClassifier _faceCascade;
    LBPHRecognizer _recognizer;
    std::map<int, QString> _labelMap;
    int _nextLabel = 0;
    cv::Size _faceSize = cv::Size(100, 100);
    bool _enabled = true;
    QMutex _mutex;   // 保护 _recognizer/_labelMap/_allSamples（分析线程 vs GUI 注册）

    std::vector<cv::Mat> _allSamples;
    std::vector<int> _allLabels;
};

#endif // FACERECOGNITION_H
