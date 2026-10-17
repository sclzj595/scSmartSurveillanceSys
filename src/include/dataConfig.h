// src/include/dataConfig.h
#ifndef DATACONFIG_H
#define DATACONFIG_H

#include <QString>

class DataConfig {
public:
    static QString faceCascadePath() {
        // return "./resources/haarcascade_frontalface_default.xml";
        return "./haarcascade_frontalface_default.xml";
        // return "D:/CODE_cpp/SmartSurveillance/resources/haarcascade_frontalface_default.xml";
    }
    
    // 命中档案的最小相似度（%）。网格直方图 LBPH 经验值：同人 >55%，异人 <25%
    // 标定方法：观察陌生人标签上显示的相似度%，在本值与同人实测值之间取中
    static double faceConfidenceThreshold() {
        return 55.0;
    }

    // 事件存证根目录（PRD 10.1 分区；按日期分目录，V0.4 事件持久化）
    static QString eventsRoot() {
        return "./events";
    }
};

#endif // DATACONFIG_H