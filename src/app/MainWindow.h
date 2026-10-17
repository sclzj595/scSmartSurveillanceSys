// MainWindow.h
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#pragma execution_character_set("utf-8")

#include "VideoCaptureThread.h"
#include "AnalysisThread.h"
#include "IntrusionDetection.h"
#include "FaceRecognition.h"
#include "ObjectDetection.h"
#include "OverlayRenderer.h"
#include "EventStore.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QMouseEvent>
#include <QTimer>
#include <QMutex>
#include <QElapsedTimer>
#include <QVector>

// 线程模型（FR-01 异步识别管线）：
//   [采集线程] --store--> [FrameBank] --waitForNew--> [分析线程]（HAAR/MOG2/LBPH/禁区）
//   [GUI线程]  QTimer 30fps --load--> FrameBank --clone--> QPainter叠加标注 --> QLabel
//   GUI 线程只承担轻量绘制与缩放，重计算全部离场 —— 预览满帧、延迟有界
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
    void renderTick();                            // 渲染节拍：取最新帧 → 叠加标注 → 显示
    void onAnalysisReady(const AnalysisResult& result);  // 分析结果缓存（仅存指针级拷贝）

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
    AnalysisThread* _analysisThread;
    QTimer* _renderTimer;

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

    // 管线数据
    FrameBank* _frameBank = nullptr;      // 采集 → {渲染, 分析} 的唯一帧通道
    cv::Mat _lastFrame;                   // 最近一帧（注册人脸用，GUI 线程专用）
    AnalysisResult _lastResult;           // 最近一次分析结果（_resultMutex 保护）
    QMutex _resultMutex;
    OverlayRenderer::Hud _hud;            // 诊断角标数据

    // 事件持久化（V0.4 / PRD 第 8 章）：业务判定 → 渲染节拍搭车截图 → 异步落盘
    EventStore* _eventStore = nullptr;
    EventThrottle _eventThrottle;         // R2 冷却
    bool _lastLeftAny = false;            // 遗留物沿检测：上帧是否存在确认遗留目标
    QVector<SecurityEvent> _pendingEvents; // 待搭车截图的事件（_resultMutex 保护）
    QPushButton* _historyBtn = nullptr;

    void detectEvents(const AnalysisResult& result);   // 事件判定（业务层规则）

    // 渲染帧率统计
    QElapsedTimer _renderClock;
    int _renderFrameCount = 0;
};

#endif  // MAINWINDOW_H
