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
        // 存储与 predict 同口径的 LBP 特征（98x98），而非原始灰度图。
        // 否则 cv::norm(98x98, 100x100) 尺寸不匹配 → cv::Exception → 进程崩溃
        _model[labels[i]].push_back(computeLBP(images[i]));
    }
}

void FaceRecognition::LBPHRecognizer::predict(
    const cv::Mat& src,
    int& label,
    double& confidence) const
{
    label = -1;
    confidence = DBL_MAX;
    if (src.rows < 3 || src.cols < 3) return;

    cv::Mat lbpSrc = computeLBP(src);

    // 使用迭代器遍历
    for(auto it = _model.begin(); it != _model.end(); ++it) {
        const std::vector<cv::Mat>& samples = it->second;
        for(const cv::Mat& sample : samples) {
            // 防御：尺寸不一致的历史样本直接跳过，绝不让 cv::norm 抛异常
            if(sample.empty() || sample.size() != lbpSrc.size())
                continue;
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

// L2 距离 → 0~100% 相似度（R1 口径：越高越相似；无效结果显示 -1，界面呈现"—"）
static double similarityFromDistance(double dist)
{
    if (dist >= 1.0e15)
        return -1.0;                 // 无样本/无效结果（predict 返回 DBL_MAX）
    double sim = 100.0 - dist;       // LBPH L2 距离经验范围 0~100+
    return std::max(0.0, std::min(100.0, sim));
}

void FaceRecognition::detectFaces(cv::Mat &gray, std::vector<cv::Rect> &faces) {
    if(_faceCascade.empty()) return;

    // 多尺度检测人脸（直方图均衡化由调用方负责）
    _faceCascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(60, 60));
}

void FaceRecognition::analyze(const cv::Mat& frame, std::vector<FaceResult>& out) {
    out.clear();
    if(_faceCascade.empty() || !_enabled) return;

    QMutexLocker locker(&_mutex);

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    std::vector<cv::Rect> faces;
    detectFaces(gray, faces);

    for(const cv::Rect& faceRect : faces) {
        FaceResult fr;
        fr.rect = faceRect;
        fr.similarity = -1.0;

        // 人脸过小不参与识别，避免远处误报（FR-03）
        if(faceRect.width >= 60 && faceRect.height >= 60) {
            cv::Mat faceROI = gray(faceRect);
            cv::Mat processedFace;
            cv::resize(faceROI, processedFace, _faceSize);

            int label = -1;
            double confidence = 0.0;
            _recognizer.predict(processedFace, label, confidence);

            fr.similarity = similarityFromDistance(confidence);
            if(confidence <= DataConfig::faceConfidenceThreshold() &&
               _labelMap.find(label) != _labelMap.end()) {
                fr.known = true;
                fr.name = _labelMap[label];
            }
        }

        out.push_back(fr);
        emit faceRecognized(fr.known ? fr.name : QString("陌生人"), fr.similarity);
    }
}

FaceRecognition::RegisterStatus FaceRecognition::registerFromFrame(const cv::Mat& bgrFrame, const QString& name) {
    if (bgrFrame.empty()) return RegisterStatus::NoFace;   // 调用方保证非空，此处兜底

    cv::Mat gray;
    cv::cvtColor(bgrFrame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    // 全程持锁：检测与训练、分析线程的检测互斥（同一 CascadeClassifier
    // 不允许并发 detectMultiScale，否则概率性崩溃）
    QMutexLocker locker(&_mutex);

    std::vector<cv::Rect> faces;
    detectFaces(gray, faces);
    if (faces.empty()) return RegisterStatus::NoFace;

    // 选择最大人脸
    auto largest = std::max_element(faces.begin(), faces.end(),
        [](const cv::Rect& a, const cv::Rect& b) {
            return a.area() < b.area();
        });

    // 与 analyze 同一口径：人脸过小不参与识别，也不允许注册
    if (largest->width < 60 || largest->height < 60) return RegisterStatus::FaceTooSmall;

    registerFaceLocked(gray(*largest), name);
    return RegisterStatus::Ok;
}

void FaceRecognition::registerFaceLocked(const cv::Mat& face, const QString& name) {
    // 前置条件：_mutex 已由调用方持有
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