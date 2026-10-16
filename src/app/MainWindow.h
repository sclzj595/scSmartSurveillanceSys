// MainWindow.h
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#pragma execution_character_set("utf-8")

#include "VideoCaptureThread.h"
#include "IntrusionDetection.h"
#include "FaceRecognition.h"
#include "ObjectDetection.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QMouseEvent>

class MainWindow : public QWidget
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    
private slots:
    void updateVideoFrame(const QImage &image);
    void toggleTheme();

private:
    void initUI();
    void applyTheme(const QString& theme);
    void saveThemeConfig(); 

    // UI组件
    QLabel* _videoLabel;
    QLabel* _logLabel;
    QPushButton* _themeToggleBtn;
    QPushButton* _faceRecogBtn;
    QPushButton* _forbiddenZoneBtn;
    QPushButton* _registerFaceBtn;
    VideoCaptureThread* _captureThread;

    // 主题控制
    QString _currentTheme;
    QSettings* _settings;

    // 功能模块
    IntrusionDetection* _intrusionDetector;
    FaceRecognition* _faceRecognizer;
    ObjectDetection* _objectDetector;
    
    // 禁区绘制相关
    bool _drawingForbiddenZone = false;
    QPoint _zoneStart;
    QPoint _zoneEnd;
    
    cv::Mat _lastFrame;
};

#endif  // MAINWINDOW_H