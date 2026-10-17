// FaceRecognition.cpp —— 算法库适配器（算法本体在 ../FaceAlgorithm 独立 DLL）
#include "vision/FaceRecognition.h"
#include <QDebug>
#include <QMutexLocker>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

FaceRecognition::FaceRecognition(QObject *parent) : QObject(parent)
{
    // 级联配置来自业务配置（算法库保持业务无关）
    if (!_algo.loadCascade(DataConfig::faceCascadePath().toStdString())) {
        qWarning() << "Failed to load face cascade:" << DataConfig::faceCascadePath();
    } else {
        qInfo() << "Face cascade loaded successfully.";
    }
    _algo.setKnownThreshold(DataConfig::faceConfidenceThreshold());

    // D6：优先恢复持久化档案（模型 + label→姓名）。恢复成功则内置样本不再加载，
    // label 空间以档案为准，避免预设样本与持久化 label 冲突
    if (loadArchive()) {
        qInfo() << "Face archive restored:" << _labelMap.size() << "persons,"
                << _algo.sampleCount(_labelMap.begin()->first) << "+ samples";
        return;
    }

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

    // D6：注册即落盘（模型 + 标签映射），重启后档案自动恢复
    if (!saveArchive())
        qWarning() << "Face archive save failed (registration kept in memory):" << trimmed;

    qInfo() << "Registered face:" << trimmed << "(Label:" << label
            << ") Samples:" << _algo.sampleCount(label);
    return true;
}

// ---- MediaLink 服务层数据源 ----
// 序列化在锁内完成（QJson 构建快，httplib 线程直接拿 bytes，不共享 Qt 对象）
QByteArray FaceRecognition::personsJsonBytes() const
{
    QMutexLocker locker(&_mutex);
    QJsonArray arr;
    for (const auto& pair : _labelMap) {
        QJsonObject o;
        o.insert("label", pair.first);
        o.insert("name", pair.second);
        o.insert("samples", _algo.sampleCount(pair.first));
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// ---- 档案持久化（D6 / V0.5）----
// 模型 YAML：labels 与 descs 两条平行序列（FaceAlgorithm 内部存取）
// 标签映射：JSON 数组 [{label, name}]，与 YAML 的 label 空间一一对应
bool FaceRecognition::loadArchive()
{
    if (!_algo.loadModel(DataConfig::faceModelPath().toStdString()))
        return false;

    QFile f(DataConfig::faceLabelsPath());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray())
        return false;

    std::map<int, QString> restored;
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        const int label = o.value("label").toInt(-1);
        const QString name = o.value("name").toString();
        if (label >= 0 && !name.isEmpty())
            restored[label] = name;
    }
    if (restored.empty())
        return false;               // 空档案视为未恢复，走内置样本路径
    _labelMap = std::move(restored);
    return true;
}

bool FaceRecognition::saveArchive()   // 前置条件：_mutex 已持有
{
    if (!_algo.saveModel(DataConfig::faceModelPath().toStdString()))
        return false;

    QJsonArray arr;
    for (const auto& pair : _labelMap) {
        QJsonObject o;
        o.insert("label", pair.first);
        o.insert("name", pair.second);
        arr.append(o);
    }
    QFile f(DataConfig::faceLabelsPath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(arr).toJson());
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
