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
    
    static double faceConfidenceThreshold() {
        return 100.0;
    }
};

#endif // DATACONFIG_H