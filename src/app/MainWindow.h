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
#include <QRubberBand>
#include <medialink/MediaLink.h>

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
    // 开关型按钮的 on/off 视觉态（动态属性 + 重刷样式，QSS 里 QPushButton[state="on"]）
    static void setToggleState(QPushButton* btn, bool on);
    // 标题栏状态 chips（ok → 绿 / bad → 红）
    void setChipState(QLabel* chip, const QString& text, bool ok);
    void updateSourceChip();   // 依 sys_param.ini 当前源配置回显

    // 视频源配置（V0.6 统一源接口）：sys_param.ini 持久化 + 切换对话框
    VideoSourceConfig loadSourceConfig() const;
    void saveSourceConfig(const VideoSourceConfig& cfg);
    void showSourceDialog();

    // UI组件
    QLabel* _videoLabel;
    QLabel* _logLabel;
    QPushButton* _themeToggleBtn;
    QPushButton* _faceRecogBtn;
    QPushButton* _forbiddenZoneBtn;
    QPushButton* _registerFaceBtn;
    QPushButton* _sourceBtn = nullptr;    // 视频源切换（V0.6：USB / RTSP）
    QLabel* _srcChip = nullptr;           // 标题栏：当前视频源（USB0 / RTSP url）
    QLabel* _svcChip = nullptr;           // 标题栏：服务层状态（HTTP :8080 · WS :8081）
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
    // 坐标链（D5 修复）：MainWindow 鼠标坐标 → mapFrom 映射到 label 局部 →
    // 按 KeepAspectRatio 反缩放 + 居中留边补偿 → 源帧坐标（分析/渲染/存证统一空间）
    bool _drawingForbiddenZone = false;
    QPoint _zoneStart;
    QPoint _zoneEnd;
    QRubberBand* _rubberBand = nullptr;                      // 绘制预览（label 局部坐标）
    cv::Rect mapRectToFrame(const QRect& labelRect) const;   // label 坐标 → 源帧坐标

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

    // ---- 独立服务层 MediaLink（PRD 第 9 章：HTTP 数据服务 + WebRTC 推流）----
    void initMediaLink();                              // 构造尾部调用：HTTP + providers
    mlink::MediaLink* _mediaLink = nullptr;
    QPushButton* _streamBtn = nullptr;                 // WebRTC 推流开关（默认关）
    bool _streamOn = false;

    // 渲染帧率统计
    QElapsedTimer _renderClock;
    int _renderFrameCount = 0;
};

#endif  // MAINWINDOW_H
