// MainWindow.cpp
#include "MainWindow.h"
#include "dataConfig.h"

#include <QFile>
#include <QMessageBox>
#include <QStyleFactory>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{   
    // 初始化设置
    _settings = new QSettings("sys_param.ini", QSettings::IniFormat, this);
    _currentTheme = _settings->value("Theme/current", "dark").toString();

    // 初始化功能模块
    _intrusionDetector = new IntrusionDetection(this);
    _faceRecognizer = new FaceRecognition(this);
    _objectDetector = new ObjectDetection(this);

    initUI();
    applyTheme(_currentTheme);

    // 启动视频采集线程
    _captureThread = new VideoCaptureThread(this);
    connect(_captureThread, &VideoCaptureThread::frameCaptured, this, [this](cv::Mat frame) {
        frame.copyTo(_lastFrame);
        
        // 处理帧数据
        std::vector<cv::Rect> faces;
        if (_faceRecognizer) {
            _faceRecognizer->processFrame(frame);
            faces = _faceRecognizer->getLastDetectedFaces();
        }
        
        if (_objectDetector) {
            _objectDetector->processFrame(frame);
        }
        
        if (_intrusionDetector && _intrusionDetector->isZoneActive()) {
            _intrusionDetector->processFrame(frame, faces);
        }
        
        // 转换为QImage并显示
        QImage image(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888);
        updateVideoFrame(image);
    });
    _captureThread->startCapture(0);
}

MainWindow::~MainWindow()
{
    // 主题保存 然后关闭config
    saveThemeConfig();
    if (_settings)   delete _settings;
}

void MainWindow::initUI()
{
    // 主布局
    auto* mainLayout = new QVBoxLayout(this);

    // 视频显示
    _videoLabel = new QLabel(this);
    _videoLabel->setAlignment(Qt::AlignCenter);      // 居中
    _videoLabel->setMinimumSize(640, 480);
    _videoLabel->setObjectName("videoLabel");        // 等会便于管理
    mainLayout->addWidget(_videoLabel, 8);

    // 日志
    _logLabel = new QLabel("系统日志：准备就绪", this);
    _logLabel->setFrameStyle(QFrame::Box);
    _logLabel->setObjectName("logLabel");
    mainLayout->addWidget(_logLabel, 2);  // 20%高度

    // ===== 控制按钮区域 =====
    QHBoxLayout *btnLayout = new QHBoxLayout();

    // Theme
    _themeToggleBtn = new QPushButton(this);
    _themeToggleBtn->setObjectName("themeToggleBtn");
    connect(_themeToggleBtn, &QPushButton::clicked, this, &MainWindow::toggleTheme);
    btnLayout->addWidget(_themeToggleBtn);

    // 人脸识别
    _faceRecogBtn = new QPushButton("人脸识别: 关闭", this);
    connect(_faceRecogBtn, &QPushButton::clicked, this, [this]() {
        // 切换人脸识别状态
        static bool faceRecognitionEnabled = false;
        faceRecognitionEnabled = !faceRecognitionEnabled;
        
        if (faceRecognitionEnabled) {
            _faceRecogBtn->setText("人脸识别: 开启");
            _logLabel->setText("人脸识别已启用");
            // 通知人脸识别模块启用
            _faceRecognizer->setEnabled(true);
        } else {
            _faceRecogBtn->setText("人脸识别: 关闭");
            _logLabel->setText("人脸识别已禁用");
            // 通知人脸识别模块禁用
            _faceRecognizer->setEnabled(false);
        }
    });
    btnLayout->addWidget(_faceRecogBtn);

    // 禁区绘制
    _forbiddenZoneBtn = new QPushButton("设置禁区", this);
    _forbiddenZoneBtn->setCheckable(true);
    connect(_forbiddenZoneBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (checked) {
            _videoLabel->setCursor(Qt::CrossCursor);
            _logLabel->setText("请在视频区域绘制禁区矩形");
        } else {
            _videoLabel->unsetCursor();
        }
    });
    btnLayout->addWidget(_forbiddenZoneBtn);

    // 注册人脸
    _registerFaceBtn = new QPushButton("注册人脸", this);
    connect(_registerFaceBtn, &QPushButton::clicked, this, [this]() {
        // 获取当前帧并注册最大人脸
        if (!_lastFrame.empty()) {
            cv::Mat gray;
            cv::cvtColor(_lastFrame, gray, cv::COLOR_BGR2GRAY);
            
            // 检测人脸
            std::vector<cv::Rect> faces;
            _faceRecognizer->detectFaces(gray, faces);
            
            if (!faces.empty()) {
                // 选择最大人脸
                auto largest = std::max_element(faces.begin(), faces.end(),
                    [](const cv::Rect& a, const cv::Rect& b) {
                        return a.area() < b.area();
                    });
                
                cv::Mat faceROI = gray(*largest);
                _faceRecognizer->registerFace(faceROI, "新用户");
                
                _logLabel->setText("人脸注册成功: 新用户");
            }
        }
    });
    btnLayout->addWidget(_registerFaceBtn);

    // 主布局里面加按钮布局
    mainLayout->addLayout(btnLayout);

    setWindowTitle("智能安防监控系统");
    resize(1200, 800);
}

void MainWindow::applyTheme(const QString &theme)
{
    if (theme == "dark") {
        // ===== 深色主题 =====
        setStyleSheet(R"(
            QWidget {
                background-color: #2D2D30;
                color: #E0E0E0;
                font-family: 'Segoe UI', Arial, sans-serif;
                font-size: 12px;
            }
            QLabel#videoLabel {
                background-color: black;
            }
            QLabel#logLabel {
                background-color: #1E1E1E;
                color: #A0A0A0;
                padding: 8px;
                border: 1px solid #3F3F46;
                border-radius: 4px;
            }
            QPushButton {
                background-color: #005792;
                color: white;
                border: none;
                padding: 8px 16px;
                border-radius: 4px;
                min-width: 100px;
            }
            QPushButton:hover {
                background-color: #006DB2;
            }
            QPushButton:pressed {
                background-color: #004A7A;
            }
            QPushButton#themeToggleBtn {
                background-color: #27AE60;
            }
        )");
        _themeToggleBtn->setText("切换到浅色主题");
    } else {
        // ===== 浅色主题 =====
        setStyleSheet(R"(
            QWidget {
                background-color: #F0F0F0;
                color: #333333;
                font-family: 'Segoe UI', Arial, sans-serif;
                font-size: 12px;
            }
            QLabel#videoLabel {
                background-color: #E0E0E0;
            }
            QLabel#logLabel {
                background-color: white;
                color: #555555;
                padding: 8px;
                border: 1px solid #CCCCCC;
                border-radius: 4px;
            }
            QPushButton {
                background-color: #3498DB;
                color: white;
                border: none;
                padding: 8px 16px;
                border-radius: 4px;
                min-width: 100px;
            }
            QPushButton:hover {
                background-color: #5DADE2;
            }
            QPushButton:pressed {
                background-color: #2E86C1;
            }
            QPushButton#themeToggleBtn {
                background-color: #E74C3C;
            }
        )");
        _themeToggleBtn->setText("切换到深色主题");
    }

    // 更新主题状态
    _currentTheme = theme;
}

void MainWindow::saveThemeConfig()
{
    _settings->setValue("Theme/current", _currentTheme);
    _settings->sync();       // 写入ini
}

void MainWindow::closeEvent(QCloseEvent *event)
{   
    // 有实例资源和运行状态下 才存在关闭逻辑
    if (_captureThread && _captureThread->isRunning()) {  
        _captureThread->requestInterruption();
        _captureThread->wait();
    }
    event->accept();
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (_forbiddenZoneBtn->isChecked() && event->button() == Qt::LeftButton) {
        _drawingForbiddenZone = true;
        _zoneStart = event->pos();
        _zoneEnd = event->pos();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (_drawingForbiddenZone) {
        _zoneEnd = event->pos();
        update(); // 触发重绘
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (_drawingForbiddenZone && event->button() == Qt::LeftButton) {
        _drawingForbiddenZone = false;
        _forbiddenZoneBtn->setChecked(false);
        
        // 计算禁区坐标
        QRect videoRect = _videoLabel->rect();
        QPoint adjustedStart = QPoint(
            _zoneStart.x() - videoRect.x(),
            _zoneStart.y() - videoRect.y()
        );
        QPoint adjustedEnd = QPoint(
            _zoneEnd.x() - videoRect.x(),
            _zoneEnd.y() - videoRect.y()
        );
        
        // 设置禁区
        cv::Rect zone(
            std::min(adjustedStart.x(), adjustedEnd.x()),
            std::min(adjustedStart.y(), adjustedEnd.y()),
            std::abs(adjustedEnd.x() - adjustedStart.x()),
            std::abs(adjustedEnd.y() - adjustedStart.y())
        );
        
        _intrusionDetector->setForbiddenZone(zone);
        
        // 更新日志
        _logLabel->setText(QString("禁区设置: (%1,%2) %3x%4")
            .arg(zone.x).arg(zone.y).arg(zone.width).arg(zone.height));
    }
}

void MainWindow::updateVideoFrame(const QImage &image) {
    _videoLabel->setPixmap(QPixmap::fromImage(image.scaled(
        _videoLabel->width(), _videoLabel->height(), Qt::KeepAspectRatio
    )));
}

void MainWindow::toggleTheme()
{
    QString newTheme = (_currentTheme == "dark") ? "light" : "dark";
    applyTheme(newTheme);
    saveThemeConfig();
}