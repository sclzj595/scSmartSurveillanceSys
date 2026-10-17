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

// 网格直方图描述子：LBP 图 4x4 网格，每格 256 维 L1 归一化直方图，拼接为 1x4096 CV_32F
// 相比逐像素 L2（对姿态/光照极端敏感、阈值无法标定），直方图比对是 LBPH 的标准形态
cv::Mat FaceRecognition::LBPHRecognizer::describe(const cv::Mat& gray100) const {
    const cv::Mat lbp = computeLBP(gray100);          // 98x98
    const int cells = 4;
    const int bins = 256;
    const int cellW = lbp.cols / cells;
    const int cellH = lbp.rows / cells;
    cv::Mat desc = cv::Mat::zeros(1, cells * cells * bins, CV_32F);
    for (int cy = 0; cy < cells; ++cy) {
        for (int cx = 0; cx < cells; ++cx) {
            const int base = (cy * cells + cx) * bins;
            cv::Mat cellHist = desc(cv::Rect(base, 0, bins, 1));
            const cv::Mat cell = lbp(cv::Rect(cx * cellW, cy * cellH, cellW, cellH));
            for (int y = 0; y < cell.rows; ++y) {
                const uchar* p = cell.ptr<uchar>(y);
                for (int x = 0; x < cell.cols; ++x)
                    cellHist.at<float>(p[x]) += 1.f;
            }
            const double s = cv::sum(cellHist)[0];
            if (s > 0) cellHist /= static_cast<float>(s);
        }
    }
    return desc;
}

void FaceRecognition::LBPHRecognizer::train(
    const std::vector<cv::Mat>& images,
    const std::vector<int>& labels)
{
    for(size_t i = 0; i < images.size(); i++) {
        // 存储网格直方图描述子（与 predict 同口径），杜绝尺寸/口径不一致导致的异常
        _model[labels[i]].push_back(describe(images[i]));
    }
}

void FaceRecognition::LBPHRecognizer::predict(
    const cv::Mat& src,
    int& label,
    double& confidence) const
{
    label = -1;
    confidence = DBL_MAX;
    if (src.rows < 8 || src.cols < 8) return;

    const cv::Mat query = describe(src);

    for(auto it = _model.begin(); it != _model.end(); ++it) {
        const std::vector<cv::Mat>& samples = it->second;
        for(const cv::Mat& sample : samples) {
            if(sample.empty() || sample.size() != query.size())
                continue;
            const double dist = cv::norm(query, sample, cv::NORM_L2);
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

// 网格直方图 L2 距离 → 0~100% 相似度（R1 口径：越高越相似；无效结果 -1，界面呈现"—"）
// 距离值域 0~5.66：0=完全一致；≥3.33 映射为 0%。经验：同人 <1.5（>55%），异人 >2.5（<25%）
static double similarityFromDistance(double dist)
{
    if (dist >= 1.0e15)
        return -1.0;                 // 无样本/无效结果（predict 返回 DBL_MAX）
    double sim = 100.0 - dist * 30.0;
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
            if(fr.similarity >= DataConfig::faceConfidenceThreshold() &&
               _labelMap.find(label) != _labelMap.end()) {
                fr.known = true;
                fr.name = _labelMap[label];
            }
        }

        out.push_back(fr);
        emit faceRecognized(fr.known ? fr.name : QString("陌生人"), fr.similarity);
    }
}

FaceRecognition::RegisterDetection FaceRecognition::detectFaceForRegister(const cv::Mat& bgrFrame) {
    RegisterDetection det;
    if (bgrFrame.empty()) return det;   // status = NoFace

    cv::Mat gray;
    cv::cvtColor(bgrFrame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    // 全程持锁：检测、建议名生成与 analyze 的检测互斥（同一 CascadeClassifier
    // 不允许并发 detectMultiScale，否则概率性崩溃）
    QMutexLocker locker(&_mutex);

    std::vector<cv::Rect> faces;
    detectFaces(gray, faces);
    if (faces.empty()) return det;

    // 选择最大人脸
    auto largest = std::max_element(faces.begin(), faces.end(),
        [](const cv::Rect& a, const cv::Rect& b) {
            return a.area() < b.area();
        });

    // 与 analyze 同一口径：人脸过小不参与识别，也不允许注册
    if (largest->width < 60 || largest->height < 60) {
        det.status = RegisterStatus::FaceTooSmall;
        return det;
    }

    det.status = RegisterStatus::Ok;
    det.face = gray(*largest).clone();  // 持有私有副本，弹窗/入库期间不受后续帧影响
    det.suggestedName = suggestedNameLocked();
    return det;
}

bool FaceRecognition::commitRegister(const cv::Mat& grayFace, const QString& name) {
    const QString trimmed = name.trimmed();
    if (grayFace.empty() || trimmed.isEmpty()) return false;

    QMutexLocker locker(&_mutex);
    registerFaceLocked(grayFace, trimmed);
    return true;
}

// 建议名"用户N"：取最小的未占用编号（前置条件：_mutex 已持有）
QString FaceRecognition::suggestedNameLocked() const {
    for (int i = 1; ; ++i) {
        const QString candidate = QStringLiteral("用户%1").arg(i);
        bool used = false;
        for (const auto& pair : _labelMap) {
            if (pair.second == candidate) { used = true; break; }
        }
        if (!used) return candidate;
    }
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