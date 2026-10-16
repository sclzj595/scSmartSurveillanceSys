// FaceRecognition.cpp
#include "FaceRecognition.h"
#include <QDebug>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <algorithm> // 添加头文件用于std::find

FaceRecognition::LBPHRecognizer::LBPHRecognizer() {}

cv::Mat FaceRecognition::LBPHRecognizer::computeLBP(const cv::Mat& src) const {
    cv::Mat dst = cv::Mat::zeros(src.rows-2, src.cols-2, CV_8UC1);
    
    for(int i = 1; i < src.rows-1; i++) {
        for(int j = 1; j < src.cols-1; j++) {
            uchar center = src.at<uchar>(i, j);
            uchar code = 0;
            code |= (src.at<uchar>(i-1, j-1) > center) << 7;
            code |= (src.at<uchar>(i-1, j  ) > center) << 6;
            code |= (src.at<uchar>(i-1, j+1) > center) << 5;
            code |= (src.at<uchar>(i  , j+1) > center) << 4;
            code |= (src.at<uchar>(i+1, j+1) > center) << 3;
            code |= (src.at<uchar>(i+1, j  ) > center) << 2;
            code |= (src.at<uchar>(i+1, j-1) > center) << 1;
            code |= (src.at<uchar>(i  , j-1) > center) << 0;
            dst.at<uchar>(i-1, j-1) = code;
        }
    }
    return dst;
}

void FaceRecognition::LBPHRecognizer::train(
    const std::vector<cv::Mat>& images, 
    const std::vector<int>& labels) 
{
    for(size_t i = 0; i < images.size(); i++) {
        // 现在_model存储的是样本向量
        _model[labels[i]].push_back(images[i]);
    }
}

void FaceRecognition::LBPHRecognizer::predict(
    const cv::Mat& src, 
    int& label, 
    double& confidence) const 
{
    cv::Mat lbpSrc = computeLBP(src);
    label = -1;
    confidence = DBL_MAX;
    
    // 使用迭代器遍历
    for(auto it = _model.begin(); it != _model.end(); ++it) {
        const std::vector<cv::Mat>& samples = it->second;
        for(const cv::Mat& sample : samples) {
            double dist = cv::norm(lbpSrc, sample, cv::NORM_L2);
            if(dist < confidence) {
                confidence = dist;
                label = it->first;
            }
        }
    }
}

FaceRecognition::FaceRecognition(QObject *parent) : QObject(parent)
{
    if(!_faceCascade.load(DataConfig::faceCascadePath().toStdString())) {
        qWarning() << "Failed to load face cascade: " << DataConfig::faceCascadePath();
    } else {
        qInfo() << "Face cascade loaded successfully.";
    }
    
    // 初始化人脸数据库
    const int personCount = 5;
    std::vector<cv::Mat> sampleImages;
    std::vector<int> labels;
    
    // 修复3：确保_nextLabel正确初始化
    _nextLabel = 1; // 明确初始化标签计数器
    
    for(int i = 1; i <= personCount; i++) {
        cv::Mat face = cv::imread(QString(":/samples/person%1.jpg").arg(i).toStdString(), 0);
        if(!face.empty()) {
            cv::resize(face, face, _faceSize);
            sampleImages.push_back(face);
            labels.push_back(i);
            _labelMap[i] = QString("User %1").arg(i);
        }
        _nextLabel = i + 1; // 更新到下一个可用标签
    }
    
    // 训练识别器
    if(!sampleImages.empty()) {
        _recognizer.train(sampleImages, labels);
        // 修复4：保存所有训练样本
        _allSamples = sampleImages;
        _allLabels = labels;
        
        qInfo() << "Face recognizer trained with" << sampleImages.size() << "samples";
    }
}

FaceRecognition::~FaceRecognition() {}

void FaceRecognition::detectFaces(cv::Mat &gray, std::vector<cv::Rect> &faces) {
    if(_faceCascade.empty()) return;
    
    // 直方图均衡化提高对比度
    cv::equalizeHist(gray, gray);
    
    // 多尺度检测人脸
    _faceCascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(60, 60));
    
    // 存储最新检测结果
    _lastFaces = faces;
}

void FaceRecognition::processFrame(cv::Mat& frame) {
    if(_faceCascade.empty() || !_enabled) return;
    
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    std::vector<cv::Rect> faces;
    detectFaces(gray, faces);
    
    for(const cv::Rect& faceRect : faces) {
        cv::Mat faceROI = gray(faceRect);
        cv::Mat processedFace;
        cv::resize(faceROI, processedFace, _faceSize);
        
        int label = -1;
        double confidence = 0.0;
        _recognizer.predict(processedFace, label, confidence);
        
        QString name = "Unknown";
        if(confidence < DataConfig::faceConfidenceThreshold() && 
           _labelMap.find(label) != _labelMap.end()) {
            name = _labelMap[label];
        }
        
        // 绘制人脸矩形和识别结果
        cv::rectangle(frame, faceRect, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, name.toStdString(), 
                    cv::Point(faceRect.x, faceRect.y - 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, QString::number(confidence, 'f', 2).toStdString(), 
                    cv::Point(faceRect.x, faceRect.y + faceRect.height + 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1);
        
        emit faceRecognized(name, confidence);
    }
}

void FaceRecognition::registerFace(const cv::Mat& face, const QString& name) {
    cv::Mat processedFace;
    if(face.channels() > 1) {
        cv::cvtColor(face, processedFace, cv::COLOR_BGR2GRAY);
    } else {
        processedFace = face.clone();
    }
    
    // 调整大小并标准化
    cv::resize(processedFace, processedFace, _faceSize);
    cv::equalizeHist(processedFace, processedFace);
    
    // 修复5：检查是否已存在相同名称
    int label = -1;
    for(const auto& pair : _labelMap) {
        if(pair.second == name) {
            label = pair.first;
            break;
        }
    }
    
    // 如果不存在则创建新标签
    if(label == -1) {
        label = _nextLabel++;
        _labelMap[label] = name;
    }
    
    // 修复6：增量训练（保留历史数据）
    _allSamples.push_back(processedFace);
    _allLabels.push_back(label);
    
    // 使用所有样本重新训练
    _recognizer.train(_allSamples, _allLabels);
    
    qInfo() << "Registered face:" << name << "(Label:" << label << ") Total samples:" << _allSamples.size();
}