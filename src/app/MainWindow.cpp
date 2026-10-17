// MainWindow.cpp
#include "MainWindow.h"
#include "dataConfig.h"

#include <QFile>
#include <QMessageBox>
#include <QStyleFactory>
#include <QCloseEvent>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>

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

    // ===== 异步识别管线（FR-01）=====
    // 跨线程队列信号的自定义类型必须先注册
    qRegisterMetaType<AnalysisResult>("AnalysisResult");
    qRegisterMetaType<cv::Rect>("cv::Rect");

    _frameBank = new FrameBank;

    // 采集线程：摄像头 → FrameBank（覆盖式写入，永不阻塞）
    _captureThread = new VideoCaptureThread(_frameBank, this);
    connect(_captureThread, &VideoCaptureThread::fpsChanged, this, [this](int fps) {
        Q_UNUSED(fps) // 采集帧率仅用于日志；预览实际帧率由渲染节拍统计
    });

    // 分析线程：FrameBank 最新帧 → 识别 → 结构化结果（队列信号回 GUI）
    _analysisThread = new AnalysisThread(_frameBank, _faceRecognizer, _objectDetector, _intrusionDetector, this);
    connect(_analysisThread, &AnalysisThread::analysisReady, this, &MainWindow::onAnalysisReady);
    _analysisThread->start(QThread::LowPriority);   // 低于 GUI 优先级，重计算不影响界面响应

    // 渲染节拍器：GUI 只做轻量绘制，30fps 定时从 FrameBank 取最新帧
    _renderTimer = new QTimer(this);
    connect(_renderTimer, &QTimer::timeout, this, &MainWindow::renderTick);
    _renderTimer->start(33);

    _renderClock.start();
    _captureThread->startCapture(0);
}

void MainWindow::renderTick()
{
    cv::Mat frame = _frameBank->load();
    if (frame.empty())
        return;                 // 无新帧（暂停/断流）→ 保持上一画面
    _lastFrame = frame;         // 引用计数拷贝（只读，注册人脸用）
    const qint64 frameAgeMs = _frameBank->ageMs();  // 帧龄须在 load 后立刻取，晚了不准

    // 实际预览渲染帧率统计（FR-08 诊断角标）
    ++_renderFrameCount;
    qint64 elapsed = _renderClock.elapsed();
    if (elapsed >= 1000) {
        QMutexLocker locker(&_resultMutex);
        _hud.renderFps = qRound(_renderFrameCount * 1000.0 / elapsed);
        _renderFrameCount = 0;
        _renderClock.restart();
    }

    // 渲染要修改像素，必须持有私有副本（见 FrameBank 头注释的像素竞争说明）
    cv::Mat draw = frame.clone();

    AnalysisResult result;
    {
        QMutexLocker locker(&_resultMutex);
        result = _lastResult;
    }
    const cv::Rect zone = _intrusionDetector->zone();

    QImage image(draw.data, draw.cols, draw.rows, draw.step, QImage::Format_BGR888);
    _hud.latencyMs = frameAgeMs;
    OverlayRenderer::paint(image, result.faces, result.objects, result.intrusion, zone, _hud);
    updateVideoFrame(image);
}

void MainWindow::onAnalysisReady(const AnalysisResult& result)
{
    QMutexLocker locker(&_resultMutex);
    _lastResult = result;       // 覆盖式：渲染永远用最新结果
    _hud.analysisFps = result.analysisFps;
    _hud.processMs = result.processMs;
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

    // 注册人脸（两段式异步：检测 → 弹窗预览/编辑姓名 → 入库；GUI 全程不冻结）
    _registerFaceBtn = new QPushButton("注册人脸", this);
    connect(_registerFaceBtn, &QPushButton::clicked, this, [this]() {
        if (_lastFrame.empty()) {
            _logLabel->setText("当前无画面，无法注册");
            return;
        }
        _registerFaceBtn->setEnabled(false);
        _logLabel->setText("正在检测人脸…");

        cv::Mat snapshot = _lastFrame.clone();  // 私有副本，避免与渲染/分析共享数据
        auto* watcher = new QFutureWatcher<FaceRecognition::RegisterDetection>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
            const FaceRecognition::RegisterDetection det = watcher->result();
            watcher->deleteLater();

            if (det.status != FaceRecognition::RegisterStatus::Ok) {
                _registerFaceBtn->setEnabled(true);
                _logLabel->setText(det.status == FaceRecognition::RegisterStatus::FaceTooSmall
                    ? "注册失败：人脸过小，请靠近摄像头后重试"
                    : "注册失败：未检测到人脸，请正对摄像头后重试");
                return;
            }

            // ---- 弹窗：人脸预览 + 姓名编辑 ----
            QDialog dlg(this);
            dlg.setWindowTitle(QStringLiteral("注册人脸"));
            dlg.setMinimumWidth(260);
            QVBoxLayout* lay = new QVBoxLayout(&dlg);

            cv::Mat previewMat;
            cv::resize(det.face, previewMat, cv::Size(140, 140));
            cv::cvtColor(previewMat, previewMat, cv::COLOR_GRAY2BGR);
            QImage previewImg(previewMat.data, previewMat.cols, previewMat.rows,
                              previewMat.step, QImage::Format_BGR888);
            QLabel* preview = new QLabel(&dlg);
            preview->setPixmap(QPixmap::fromImage(previewImg));
            preview->setAlignment(Qt::AlignCenter);

            QLabel* hint = new QLabel(QStringLiteral("检测到人脸，请确认预览并编辑姓名："), &dlg);
            QLineEdit* nameEdit = new QLineEdit(det.suggestedName, &dlg);
            nameEdit->setPlaceholderText(QStringLiteral("输入人员姓名"));
            nameEdit->selectAll();

            QDialogButtonBox* btns = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("注册"));
            btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

            lay->addWidget(hint);
            lay->addWidget(preview);
            lay->addWidget(nameEdit);
            lay->addWidget(btns);
            connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

            if (dlg.exec() != QDialog::Accepted) {
                _registerFaceBtn->setEnabled(true);
                _logLabel->setText("已取消注册");
                return;
            }

            QString name = nameEdit->text().trimmed();
            if (name.isEmpty())
                name = det.suggestedName;

            // ---- 第二段：入库训练（异步）----
            _logLabel->setText(QStringLiteral("正在注册「%1」…").arg(name));
            const cv::Mat face = det.face;
            auto* commitWatcher = new QFutureWatcher<bool>(this);
            connect(commitWatcher, &QFutureWatcherBase::finished, this, [this, commitWatcher, name]() {
                _registerFaceBtn->setEnabled(true);
                _logLabel->setText(commitWatcher->result()
                    ? QStringLiteral("人脸注册成功: %1").arg(name)
                    : QStringLiteral("注册失败：入库名称无效"));
                commitWatcher->deleteLater();
            });
            commitWatcher->setFuture(QtConcurrent::run([this, face, name]() {
                return _faceRecognizer->commitRegister(face, name);
            }));
        });
        watcher->setFuture(QtConcurrent::run([this, snapshot]() {
            return _faceRecognizer->detectFaceForRegister(snapshot);
        }));
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
    // 关闭顺序：先停渲染节拍 → 停分析线程 → 停采集线程（自释放设备）
    if (_renderTimer)
        _renderTimer->stop();
    if (_analysisThread && _analysisThread->isRunning()) {
        _analysisThread->shutdown();
        _analysisThread->wait(2000);
    }
    if (_captureThread && _captureThread->isRunning()) {
        _captureThread->stopCapture();
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