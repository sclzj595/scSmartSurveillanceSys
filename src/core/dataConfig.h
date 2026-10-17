// src/core/dataConfig.h
#ifndef DATACONFIG_H
#define DATACONFIG_H

#include <QString>
#include <QCoreApplication>

class DataConfig {
public:
    // 运行时数据统一锚定 exe 目录（applicationDirPath）：
    // 无论从 IDE / 双击 / 终端何种 CWD 启动，级联、档案、事件存证、
    // 参数文件全部稳定落位 build/bin，杜绝 CWD 漂移导致的静默写失败
    static QString appDir() { return QCoreApplication::applicationDirPath(); }

    static QString faceCascadePath() {
        return appDir() + "/haarcascade_frontalface_default.xml";
    }

    // 命中档案的最小相似度（%）。网格直方图 LBPH 经验值：同人 >55%，异人 <25%
    // 标定方法：观察陌生人标签上显示的相似度%，在本值与同人实测值之间取中
    static double faceConfidenceThreshold() {
        return 55.0;
    }

    // 事件存证根目录（PRD 10.1 分区；按日期分目录，V0.4 事件持久化）
    static QString eventsRoot() {
        return appDir() + "/events";
    }

    // 人脸档案持久化（D6 / V0.5）：模型 YAML（FaceAlgorithm 存取）+ label→姓名 JSON
    // 属运行时数据不入 Git（.gitignore 已收录）
    static QString faceModelPath()  { return appDir() + "/face_model.yml"; }
    static QString faceLabelsPath() { return appDir() + "/face_labels.json"; }

    // 宿主参数（主题 / 视频源配置），与运行时数据同址
    static QString settingsPath() { return appDir() + "/sys_param.ini"; }
};

#endif // DATACONFIG_H
