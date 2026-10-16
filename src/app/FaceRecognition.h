// FaceRecognition.h (文档9)
#ifndef FACERECOGNITION_H
#define FACERECOGNITION_H

#pragma execution_character_set("utf-8")

#include <QObject>
#include <opencv2/opencv.hpp>
#include <vector>
#include <map>
#include "dataConfig.h"

class FaceRecognition : public QObject
{
    Q_OBJECT

public:
    explicit FaceRecognition(QObject *parent = nullptr);
    ~FaceRecognition();

    void processFrame(cv::Mat& frame);
    void registerFace(const cv::Mat& face, const QString& name);
    void detectFaces(cv::Mat &gray, std::vector<cv::Rect> &faces);
    void setEnabled(bool enabled) { _enabled = enabled; }
    std::vector<cv::Rect> getLastDetectedFaces() const { return _lastFaces; }

signals:
    void faceRecognized(QString name, double confidence);

private:
    class LBPHRecognizer {
    public:
        LBPHRecognizer();
        cv::Mat computeLBP(const cv::Mat& src) const;
        void train(const std::vector<cv::Mat>& images, const std::vector<int>& labels);
        void predict(const cv::Mat& src, int& label, double& confidence) const;
    private:
        // std::map<int, cv::Mat> _model;
        std::map<int, std::vector<cv::Mat>> _model; 
    };

    cv::CascadeClassifier _faceCascade;
    LBPHRecognizer _recognizer;
    std::map<int, QString> _labelMap;
    int _nextLabel = 0;
    cv::Size _faceSize = cv::Size(100, 100);
    bool _enabled = true;
    std::vector<cv::Rect> _lastFaces; // 存储最新检测到的人脸

    std::vector<cv::Mat> _allSamples;
    std::vector<int> _allLabels;
    
};

#endif // FACERECOGNITION_H