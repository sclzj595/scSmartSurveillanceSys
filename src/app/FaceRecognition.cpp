// FaceRecognition.cpp —— 算法库适配器（算法本体在 ../FaceAlgorithm 独立 DLL）
#include "FaceRecognition.h"
#include <QDebug>
#include <QMutexLocker>

FaceRecognition::FaceRecognition(QObject *parent) : QObject(parent)
{
    // 级联配置来自业务配置（算法库保持业务无关）
    if (!_algo.loadCascade(DataConfig::faceCascadePath().toStdString())) {
        qWarning() << "Failed to load face cascade:" << DataConfig::faceCascadePath();
    } else {
        qInfo() << "Face cascade loaded successfully.";
    }
    _algo.setKnownThreshold(DataConfig::faceConfidenceThreshold());

    // 预置内置样本（与历史行为一致：qrc 资源缺失时静默跳过，不影响运行）
    const int personCount = 5;
    for (int i = 1; i <= personCount; i++) {
        cv::Mat face = cv::imread(QString(":/samples/person%1.jpg").arg(i).toStdString(), 0);
        if (!face.empty()) {
            _algo.addSample(face, i);
            _labelMap[i] = QString("User %1").arg(i);
        }
    }
}

FaceRecognition::~FaceRecognition() {}

void FaceRecognition::analyze(const cv::Mat& frame, std::vector<FaceResult>& out) {
    out.clear();
    if (!_enabled || frame.empty()) return;

    QMutexLocker locker(&_mutex);

    const falgo::FrameResult r = _algo.detectAndRecognize(frame);
    out.reserve(r.faces.size());
    for (const falgo::FaceHit& h : r.faces) {
        FaceResult fr;
        fr.rect = h.rect;
        fr.similarity = h.similarity;
        if (h.known) {
            auto it = _labelMap.find(h.label);
            if (it != _labelMap.end()) {
                fr.known = true;
                fr.name = it->second;
            }
        }
        out.push_back(fr);
        emit faceRecognized(fr.known ? fr.name : QString("陌生人"), fr.similarity);
    }
}

FaceRecognition::RegisterDetection FaceRecognition::detectFaceForRegister(const cv::Mat& bgrFrame) {
    RegisterDetection det;
    if (bgrFrame.empty()) return det;   // status = NoFace

    QMutexLocker locker(&_mutex);

    falgo::FaceCrop crop;
    const falgo::DetectStatus st = _algo.detectLargest(bgrFrame, crop);
    switch (st) {
    case falgo::DetectStatus::FaceTooSmall:
        det.status = RegisterStatus::FaceTooSmall;
        return det;
    case falgo::DetectStatus::Ok:
        det.status = RegisterStatus::Ok;
        det.face = crop.gray100;            // 归一化灰度人脸（私有副本）
        det.suggestedName = suggestedNameLocked();
        return det;
    case falgo::DetectStatus::NoFace:
    default:
        return det;                          // status = NoFace
    }
}

bool FaceRecognition::commitRegister(const cv::Mat& grayFace, const QString& name) {
    const QString trimmed = name.trimmed();
    if (grayFace.empty() || trimmed.isEmpty()) return false;

    QMutexLocker locker(&_mutex);

    // 同名 = 补充样本：查找已有 label，否则新建（取当前最大 label+1，保证唯一）
    int label = -1;
    for (const auto& pair : _labelMap) {
        if (pair.second == trimmed) { label = pair.first; break; }
    }
    if (label == -1)
        label = _labelMap.empty() ? 1 : _labelMap.rbegin()->first + 1;

    if (!_algo.addSample(grayFace, label))
        return false;

    _labelMap[label] = trimmed;
    qInfo() << "Registered face:" << trimmed << "(Label:" << label
            << ") Samples:" << _algo.sampleCount(label);
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
