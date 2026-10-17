// MainWindow.cpp
#include "MainWindow.h"
#include "dataConfig.h"

#include <QFile>
#include <QMessageBox>
#include <QStyleFactory>
#include <QStyle>
#include <QCloseEvent>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QComboBox>
#include <QFormLayout>
#include <QDateTime>
#include "EventHistoryDialog.h"

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

    // 事件中心（V0.4）：业务判定在 GUI 侧，落盘在独立 worker 线程（R4 失败仅提示）
    _eventStore = new EventStore(this);
    connect(_eventStore, &EventStore::storeError, this, [this](const QString& msg) {
        _logLabel->setText(QStringLiteral("事件存证异常（识别不受影响）：%1").arg(msg));
    });

    // 采集线程：视频源 → FrameBank（覆盖式写入，永不阻塞）
    // 源抽象与断流重连（D9）在采集线程内部完成，状态经 sourceStatus 回显日志栏
    _captureThread = new VideoCaptureThread(_frameBank, this);
    connect(_captureThread, &VideoCaptureThread::fpsChanged, this, [this](int fps) {
        Q_UNUSED(fps) // 采集帧率仅用于日志；预览实际帧率由渲染节拍统计
    });
    connect(_captureThread, &VideoCaptureThread::sourceStatus, this, [this](const QString& msg) {
        _logLabel->setText(msg);
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
    _captureThread->startCapture(loadSourceConfig());   // V0.6：源配置持久化恢复

    initMediaLink();   // 独立服务层（PRD 第 9 章）：HTTP 数据服务随启动开启
}

void MainWindow::renderTick()
{
    cv::Mat frame = _frameBank->load();
    if (frame.empty())
        return;                 // 无新帧（暂停/断流）→ 保持上一画面
    _lastFrame = frame;         // 引用计数拷贝（只读，注册人脸用）

    // 服务层推流（PRD 第 9 章）：渲染帧搭车喂给 MediaLink 内部最新帧槽（非阻塞）
    if (_streamOn && _mediaLink && !_lastFrame.empty())
        _mediaLink->pushFrame(_lastFrame.data, _lastFrame.cols, _lastFrame.rows,
                              int(_lastFrame.step));
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

    // 事件搭车截图：本帧标注图恰好为全分辨率存证所需，一并交给落盘线程
    QVector<SecurityEvent> pending;
    {
        QMutexLocker locker(&_resultMutex);
        pending.swap(_pendingEvents);
    }
    if (!pending.isEmpty()) {
        _eventStore->enqueue(pending, image.copy());   // copy 脱离 draw 缓冲，跨线程安全
        for (const SecurityEvent& e : pending) {       // WS 事件推送（V0.7，非阻塞入队）
            if (_mediaLink)
                _mediaLink->pushEvent(nlohmann::json{
                    {"ts", e.tsMs},
                    {"type", SecurityEvent::typeKey(e.type).toStdString()},
                    {"subject", e.subject.toStdString()},
                    {"similarity", e.similarity},
                    {"detail", e.detail.toStdString()}
                });
        }
    }
}

// 事件判定（业务层规则，PRD 8.1；引擎只给结构化结果，"什么算事件"在这里）
void MainWindow::detectEvents(const AnalysisResult& result)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<SecurityEvent> events;

    for (const FaceResult& f : result.faces) {
        if (f.known) {
            // 人员识别：同人 30s 冷却（R2）
            if (_eventThrottle.allow(QStringLiteral("face_known:%1").arg(f.name), now, 30 * 1000)) {
                SecurityEvent e;
                e.type = SecurityEvent::FaceKnown;
                e.subject = f.name;
                e.similarity = f.similarity;
                e.detail = QStringLiteral("识别到已登记人员（相似度 %1%）").arg(f.similarity, 0, 'f', 0);
                events.append(e);
            }
        } else if (f.similarity >= 0) {
            // 陌生人出现：有检测未命中；陌生人无稳定标识，按类型整体 30s 冷却
            if (_eventThrottle.allow(QStringLiteral("face_unknown"), now, 30 * 1000)) {
                SecurityEvent e;
                e.type = SecurityEvent::FaceUnknown;
                e.subject = QStringLiteral("陌生人");
                e.similarity = f.similarity;
                e.detail = QStringLiteral("出现未登记人脸（与档案最高相似度 %1%）").arg(f.similarity, 0, 'f', 0);
                events.append(e);
            }
        }
    }

    if (result.intrusion.active) {
        // 禁区入侵：10s 冷却（R2）
        if (_eventThrottle.allow(QStringLiteral("intrusion:zone"), now, 10 * 1000)) {
            SecurityEvent e;
            e.type = SecurityEvent::Intrusion;
            e.subject = QStringLiteral("禁区");
            e.detail = QStringLiteral("有目标进入禁区（目标数 %1）").arg(result.intrusion.offenders.size());
            events.append(e);
        }
    }

    // 遗留物沿检测：确认遗留出现 / 解除各触发一次（60s 保险冷却防抖动刷屏）
    const bool leftAny = !result.objects.empty();
    if (leftAny != _lastLeftAny) {
        if (_eventThrottle.allow(QStringLiteral("left_object"), now, 60 * 1000)) {
            SecurityEvent e;
            e.type = leftAny ? SecurityEvent::LeftObject : SecurityEvent::LeftCleared;
            e.subject = QStringLiteral("画面目标");
            e.detail = leftAny
                ? QStringLiteral("目标静止超过阈值，确认遗留")
                : QStringLiteral("遗留物已移走");
            events.append(e);
        }
        _lastLeftAny = leftAny;
    }

    if (!events.isEmpty()) {
        QMutexLocker locker(&_resultMutex);
        _pendingEvents += events;
    }
}

void MainWindow::onAnalysisReady(const AnalysisResult& result)
{
    {
        QMutexLocker locker(&_resultMutex);
        _lastResult = result;       // 覆盖式：渲染永远用最新结果
        _hud.analysisFps = result.analysisFps;
        _hud.processMs = result.processMs;
    }
    detectEvents(result);   // 事件判定（轻量：哈希查表 + 沿比较，GUI 线程可承受）
}

MainWindow::~MainWindow()
{
    // 服务层先停（内部 join HTTP/推流线程，避免窗口析构后 Queued 日志悬空）
    if (_mediaLink) {
        delete _mediaLink;          // 析构内 stopStream + stopHttp
        _mediaLink = nullptr;
    }
    // 主题保存 然后关闭config
    saveThemeConfig();
    if (_settings)   delete _settings;
}

void MainWindow::initUI()
{
    // ===== 工业监控风格骨架：标题栏 / 视频区 / 控制栏 / 状态栏（零边距拼接）=====
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 标题栏：logo 色块 + 标题 + 状态 chips + 主题切换 ----
    auto* header = new QWidget(this);
    header->setObjectName("headerBar");
    auto* headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(14, 8, 14, 8);
    headerLay->setSpacing(10);

    auto* logo = new QLabel(header);
    logo->setObjectName("logoChip");
    logo->setFixedSize(6, 18);
    auto* title = new QLabel(QStringLiteral("智能安防监控"), header);
    title->setObjectName("appTitle");
    headerLay->addWidget(logo);
    headerLay->addWidget(title);
    headerLay->addSpacing(8);

    _srcChip = new QLabel(header);          // 当前视频源（updateSourceChip 回显）
    _srcChip->setObjectName("srcChip");
    _svcChip = new QLabel(header);          // 服务层 HTTP/WS 状态（initMediaLink 回显）
    _svcChip->setObjectName("svcChip");
    headerLay->addStretch();
    headerLay->addWidget(_srcChip);
    headerLay->addWidget(_svcChip);

    _themeToggleBtn = new QPushButton(header);
    _themeToggleBtn->setObjectName("themeToggleBtn");
    headerLay->addWidget(_themeToggleBtn);
    connect(_themeToggleBtn, &QPushButton::clicked, this, &MainWindow::toggleTheme);
    root->addWidget(header);

    // ---- 视频区（带边距的圆角面板）----
    auto* videoWrap = new QVBoxLayout;
    videoWrap->setContentsMargins(12, 12, 12, 8);
    _videoLabel = new QLabel(this);
    _videoLabel->setAlignment(Qt::AlignCenter);      // 居中
    _videoLabel->setMinimumSize(640, 480);
    _videoLabel->setObjectName("videoLabel");        // 等会便于管理
    _rubberBand = new QRubberBand(QRubberBand::Rectangle, _videoLabel);  // 禁区绘制预览
    videoWrap->addWidget(_videoLabel);
    root->addLayout(videoWrap, 1);

    // ---- 控制栏：全部功能按钮一排 ----
    auto* btnBar = new QWidget(this);
    btnBar->setObjectName("btnBar");
    auto* btnLayout = new QHBoxLayout(btnBar);
    btnLayout->setContentsMargins(12, 4, 12, 8);
    btnLayout->setSpacing(8);

    // 人脸识别
    _faceRecogBtn = new QPushButton("人脸识别: 关闭", this);
    connect(_faceRecogBtn, &QPushButton::clicked, this, [this]() {
        // 切换人脸识别状态
        static bool faceRecognitionEnabled = false;
        faceRecognitionEnabled = !faceRecognitionEnabled;
        
        if (faceRecognitionEnabled) {
            _faceRecogBtn->setText("人脸识别: 开启");
            setToggleState(_faceRecogBtn, true);
            _logLabel->setText("人脸识别已启用");
            // 通知人脸识别模块启用
            _faceRecognizer->setEnabled(true);
        } else {
            _faceRecogBtn->setText("人脸识别: 关闭");
            setToggleState(_faceRecogBtn, false);
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
            preview->setObjectName("previewLabel");
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

    // 历史记录（V0.4 事件持久化的查询入口，PRD 8.3）
    _historyBtn = new QPushButton("历史记录", this);
    connect(_historyBtn, &QPushButton::clicked, this, [this]() {
        EventHistoryDialog dlg(this);
        dlg.exec();
    });
    btnLayout->addWidget(_historyBtn);

    // WebRTC 推流开关（独立服务层 MediaLink：FFmpeg 管道 → MediaMTX → H5 拉流）
    _streamBtn = new QPushButton(QStringLiteral("WebRTC推流: 关"), this);
    connect(_streamBtn, &QPushButton::clicked, this, [this]() {
        if (_streamOn) {
            _mediaLink->stopStream();
            _streamOn = false;
            _streamBtn->setText(QStringLiteral("WebRTC推流: 关"));
            setToggleState(_streamBtn, false);
            _logLabel->setText(QStringLiteral("推流已停止"));
        } else {
            const mlink::StreamConfig cfg;   // 默认 tools/ffmpeg.exe → rtsp://127.0.0.1:8554/sccam
            _mediaLink->setStreamConfig(cfg);
            if (!_mediaLink->startStream())
                _logLabel->setText(QStringLiteral(
                    "推流未启动（Degraded）：未找到 tools/ffmpeg.exe；放入后重开开关即生效"));
            else
                _logLabel->setText(QStringLiteral("推流启动中，等待首帧…目标 %1").arg(QString::fromStdString(cfg.targetUrl)));
            _streamOn = true;
            _streamBtn->setText(QStringLiteral("WebRTC推流: 开"));
            setToggleState(_streamBtn, true);
        }
    });
    btnLayout->addWidget(_streamBtn);

    // 视频源切换（V0.6：统一源接口，USB / RTSP 即插即用）
    _sourceBtn = new QPushButton(QStringLiteral("切换视频源"), this);
    connect(_sourceBtn, &QPushButton::clicked, this, &MainWindow::showSourceDialog);
    btnLayout->addWidget(_sourceBtn);

    btnLayout->addStretch();      // 按钮左聚，右侧留白
    root->addWidget(btnBar);

    // ---- 状态栏：单行纤细日志（替代原 20% 高度大块）----
    _logLabel = new QLabel("系统日志：准备就绪", this);
    _logLabel->setObjectName("logLabel");
    _logLabel->setFixedHeight(30);
    root->addWidget(_logLabel);

    updateSourceChip();           // 标题栏源 chip 初值（服务 chip 由 initMediaLink 回填）

    setWindowTitle("智能安防监控系统");
    resize(1280, 860);
}

void MainWindow::setChipState(QLabel* chip, const QString& text, bool ok)
{
    chip->setText(text);
    chip->setProperty("state", ok ? "ok" : "bad");
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
}

void MainWindow::updateSourceChip()
{
    const VideoSourceConfig cfg = loadSourceConfig();
    setChipState(_srcChip, cfg.type == SourceType::RtspStream
        ? QStringLiteral("源 RTSP")
        : QStringLiteral("源 USB%1").arg(cfg.usbIndex), true);
}

void MainWindow::setToggleState(QPushButton* btn, bool on)
{
    btn->setProperty("state", on ? "on" : "off");
    btn->style()->unpolish(btn);
    btn->style()->polish(btn);
}

void MainWindow::initMediaLink()
{
    // 服务层为纯 STL 对象（非 Qt 树），析构在 ~MainWindow 手动 delete
    _mediaLink = new mlink::MediaLink;
    _mediaLink->setLogger([this](const std::string& m) {
        // httplib/推流线程回调：必须切回 GUI 线程才能碰 UI
        const QString msg = QString::fromUtf8(m.c_str());
        QMetaObject::invokeMethod(this, [this, msg] { _logLabel->setText(msg); },
                                  Qt::QueuedConnection);
    });

    // 数据注入：业务在宿主（EventStore/FaceRecognition），服务层不持有业务状态
    mlink::Provider p;
    p.events = [this](int limit, const std::string& type) {
        const QByteArray bytes =
            _eventStore->recentJsonBytes(limit, QString::fromStdString(type));
        auto j = nlohmann::json::parse(bytes.constBegin(), bytes.constEnd(),
                                       nullptr, /*allow_exceptions=*/false);
        return j.is_discarded() ? nlohmann::json::array() : j;
    };
    p.persons = [this]() {
        const QByteArray bytes = _faceRecognizer->personsJsonBytes();
        auto j = nlohmann::json::parse(bytes.constBegin(), bytes.constEnd(),
                                       nullptr, /*allow_exceptions=*/false);
        return j.is_discarded() ? nlohmann::json::array() : j;
    };
    _mediaLink->setProviders(p);

    if (_mediaLink->startHttp(8080)) {
        const bool wsOk = _mediaLink->startWs(8081);
        setChipState(_svcChip, wsOk ? QStringLiteral("服务 :8080 · WS :8081")
                                    : QStringLiteral("服务 :8080 · WS 未启动"), true);
        _logLabel->setText(QStringLiteral(
            "HTTP 数据服务 :8080 已开启（/api/health /api/events /api/persons /api/stream）%1")
            .arg(wsOk ? QStringLiteral("；WS 事件推送 :8081 已开启")
                      : QStringLiteral("；WS :8081 端口被占用，事件推送未开启")));
    } else {
        setChipState(_svcChip, QStringLiteral("服务未启动"), false);
        _logLabel->setText(QStringLiteral("HTTP :8080 端口被占用，数据服务未开启"));
    }
}

VideoSourceConfig MainWindow::loadSourceConfig() const
{
    VideoSourceConfig cfg;
    const QString t = _settings->value("Source/type", "usb").toString();
    if (t == "rtsp") {
        cfg.type = SourceType::RtspStream;
        cfg.url = _settings->value("Source/url").toString();
    }
    cfg.usbIndex = _settings->value("Source/usbIndex", 0).toInt();
    return cfg;
}

void MainWindow::saveSourceConfig(const VideoSourceConfig& cfg)
{
    _settings->setValue("Source/type", cfg.type == SourceType::RtspStream ? "rtsp" : "usb");
    _settings->setValue("Source/usbIndex", cfg.usbIndex);
    _settings->setValue("Source/url", cfg.url);
    _settings->sync();
}

void MainWindow::showSourceDialog()
{
    const VideoSourceConfig cur = loadSourceConfig();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("切换视频源"));
    dlg.setMinimumWidth(360);
    auto* form = new QFormLayout(&dlg);

    QComboBox* typeBox = new QComboBox(&dlg);
    typeBox->addItem(QStringLiteral("USB 摄像头 0"), 0);
    typeBox->addItem(QStringLiteral("USB 摄像头 1"), 1);
    typeBox->addItem(QStringLiteral("RTSP 网络流"), 2);
    typeBox->setCurrentIndex(cur.type == SourceType::RtspStream ? 2
                            : qBound(0, cur.usbIndex, 1));

    QLineEdit* urlEdit = new QLineEdit(cur.url, &dlg);
    urlEdit->setPlaceholderText(QStringLiteral("rtsp://user:pass@192.168.1.64:554/stream1"));
    urlEdit->setEnabled(cur.type == SourceType::RtspStream);
    connect(typeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg,
            [typeBox, urlEdit](int idx) { urlEdit->setEnabled(idx == 2); });

    QDialogButtonBox* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    form->addRow(QStringLiteral("源类型"), typeBox);
    form->addRow(QStringLiteral("RTSP 地址"), urlEdit);
    form->addRow(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;

    VideoSourceConfig cfg;
    if (typeBox->currentIndex() == 2) {
        cfg.type = SourceType::RtspStream;
        cfg.url = urlEdit->text().trimmed();
        if (cfg.url.isEmpty()) {
            _logLabel->setText(QStringLiteral("RTSP 地址为空，未切换"));
            return;
        }
    } else {
        cfg.usbIndex = typeBox->currentData().toInt();
    }

    saveSourceConfig(cfg);
    updateSourceChip();           // 标题栏源 chip 同步
    if (_captureThread->switchSource(cfg))
        _logLabel->setText(QStringLiteral("视频源切换中…"));   // 后续状态经 sourceStatus 回显
    else
        _logLabel->setText(QStringLiteral("视频源切换失败：采集线程未就绪"));
}

void MainWindow::applyTheme(const QString &theme)
{
    // 工业监控风格双主题：面板化布局 + 圆角控件 + 状态色语义
    // （深色：深蓝灰底 / 浅色：冷灰白底；两套色板同一套规则）
    if (theme == "dark") {
        setStyleSheet(R"(
            * { outline: none; }
            QWidget {
                background-color: #151a21;
                color: #dde3ec;
                font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;
                font-size: 13px;
            }
            QLabel { background: transparent; }

            /* ---- 标题栏 ---- */
            QWidget#headerBar {
                background-color: #1b222c;
                border-bottom: 1px solid #262e3a;
            }
            QLabel#logoChip { background-color: #3b82f6; border-radius: 1px; }
            QLabel#appTitle { color: #f2f5fa; font-size: 15px; font-weight: 600; }
            QLabel#srcChip, QLabel#svcChip {
                border: 1px solid #2e3a49; border-radius: 10px;
                padding: 2px 10px; color: #9aa5b5; background-color: #202834;
                font-size: 12px;
            }
            QLabel#srcChip[state="ok"], QLabel#svcChip[state="ok"] {
                color: #4ade80; border-color: #2f5e46;
            }
            QLabel#svcChip[state="bad"] {
                color: #f87171; border-color: #5b2c2c;
            }

            /* ---- 视频面板 ---- */
            QLabel#videoLabel {
                background-color: #060809;
                border: 1px solid #262e3a;
                border-radius: 8px;
            }

            /* ---- 控制栏按钮 ---- */
            QWidget#btnBar { background: transparent; }
            QPushButton {
                background-color: #222b37;
                color: #dbe2ec;
                border: 1px solid #2e3a49;
                border-radius: 6px;
                padding: 7px 16px;
                min-width: 88px;
            }
            QPushButton:hover { background-color: #2a3543; border-color: #3d4c60; }
            QPushButton:pressed { background-color: #1f2833; }
            QPushButton:disabled { color: #5c6673; border-color: #262e3a; }
            QPushButton[state="on"] {          /* 开关型按钮：开启态绿描边 */
                color: #4ade80; border-color: #2f5e46; background-color: #182219;
            }
            QPushButton[state="on"]:hover { border-color: #3d7a5c; }
            QPushButton:checked {              /* 禁区绘制模式：琥珀提示 */
                color: #fbbf24; border-color: #8a5a12; background-color: #241d10;
            }
            QPushButton#themeToggleBtn {
                min-width: 0; padding: 5px 12px; color: #9aa5b5; font-size: 12px;
            }
            QPushButton#themeToggleBtn:hover { color: #dde3ec; }

            /* ---- 状态栏（单行日志）---- */
            QLabel#logLabel {
                background-color: #10141a;
                color: #8b95a5;
                border-top: 1px solid #262e3a;
                padding: 0 14px;
            }

            /* ---- 弹窗（注册/切源/历史）---- */
            QDialog { background-color: #1b222c; }
            QLabel#previewLabel { border: 1px solid #2e3a49; border-radius: 6px; background: #10141a; }
            QLineEdit, QComboBox {
                background-color: #10141a;
                border: 1px solid #2e3a49;
                border-radius: 6px;
                padding: 6px 10px;
                color: #dde3ec;
            }
            QLineEdit:focus, QComboBox:focus { border-color: #3b82f6; }
            QComboBox::drop-down { border: none; width: 22px; }
            QComboBox QAbstractItemView {
                background-color: #222b37; border: 1px solid #2e3a49;
                selection-background-color: #2f4368;
            }
            QPushButton#closeBtn { min-width: 72px; }

            /* ---- 历史记录表格 ---- */
            QTableWidget {
                background-color: #151a21;
                border: 1px solid #2e3a49;
                border-radius: 6px;
                gridline-color: #232b36;
                selection-background-color: #2f4368;
                selection-color: #ffffff;
            }
            QTableWidget::item { padding: 4px; }
            QHeaderView::section {
                background-color: #1b222c;
                color: #9aa5b5;
                border: none;
                border-bottom: 1px solid #2e3a49;
                padding: 6px 8px;
            }
            QScrollBar:vertical { background: #151a21; width: 10px; }
            QScrollBar::handle:vertical { background: #2e3a49; border-radius: 5px; min-height: 30px; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        )");
        _themeToggleBtn->setText("浅色");
    } else {
        setStyleSheet(R"(
            * { outline: none; }
            QWidget {
                background-color: #f3f5f8;
                color: #1f2430;
                font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;
                font-size: 13px;
            }
            QLabel { background: transparent; }

            /* ---- 标题栏 ---- */
            QWidget#headerBar {
                background-color: #ffffff;
                border-bottom: 1px solid #e2e7ee;
            }
            QLabel#logoChip { background-color: #3b82f6; border-radius: 1px; }
            QLabel#appTitle { color: #111827; font-size: 15px; font-weight: 600; }
            QLabel#srcChip, QLabel#svcChip {
                border: 1px solid #d5dce5; border-radius: 10px;
                padding: 2px 10px; color: #5b6675; background-color: #f6f8fb;
                font-size: 12px;
            }
            QLabel#srcChip[state="ok"], QLabel#svcChip[state="ok"] {
                color: #15803d; border-color: #bbe3c8;
            }
            QLabel#svcChip[state="bad"] {
                color: #b91c1c; border-color: #f0c4c4;
            }

            /* ---- 视频面板 ---- */
            QLabel#videoLabel {
                background-color: #060809;
                border: 1px solid #d5dce5;
                border-radius: 8px;
            }

            /* ---- 控制栏按钮 ---- */
            QWidget#btnBar { background: transparent; }
            QPushButton {
                background-color: #ffffff;
                color: #374151;
                border: 1px solid #d5dce5;
                border-radius: 6px;
                padding: 7px 16px;
                min-width: 88px;
            }
            QPushButton:hover { background-color: #f0f4f9; border-color: #b9c4d2; }
            QPushButton:pressed { background-color: #e6ecf3; }
            QPushButton:disabled { color: #a8b1bd; border-color: #e2e7ee; }
            QPushButton[state="on"] {
                color: #15803d; border-color: #87c9a0; background-color: #effaf3;
            }
            QPushButton[state="on"]:hover { border-color: #4fae72; }
            QPushButton:checked {
                color: #b45309; border-color: #e3b877; background-color: #fdf6e7;
            }
            QPushButton#themeToggleBtn {
                min-width: 0; padding: 5px 12px; color: #5b6675; font-size: 12px;
            }
            QPushButton#themeToggleBtn:hover { color: #1f2430; }

            /* ---- 状态栏 ---- */
            QLabel#logLabel {
                background-color: #ffffff;
                color: #6b7482;
                border-top: 1px solid #e2e7ee;
                padding: 0 14px;
            }

            /* ---- 弹窗 ---- */
            QDialog { background-color: #ffffff; }
            QLabel#previewLabel { border: 1px solid #d5dce5; border-radius: 6px; background: #f3f5f8; }
            QLineEdit, QComboBox {
                background-color: #ffffff;
                border: 1px solid #d5dce5;
                border-radius: 6px;
                padding: 6px 10px;
                color: #1f2430;
            }
            QLineEdit:focus, QComboBox:focus { border-color: #3b82f6; }
            QComboBox::drop-down { border: none; width: 22px; }
            QComboBox QAbstractItemView {
                background-color: #ffffff; border: 1px solid #d5dce5;
                selection-background-color: #dbeafe;
            }
            QPushButton#closeBtn { min-width: 72px; }

            /* ---- 历史记录表格 ---- */
            QTableWidget {
                background-color: #ffffff;
                border: 1px solid #d5dce5;
                border-radius: 6px;
                gridline-color: #e8edf3;
                selection-background-color: #dbeafe;
                selection-color: #1f2430;
            }
            QTableWidget::item { padding: 4px; }
            QHeaderView::section {
                background-color: #f6f8fb;
                color: #5b6675;
                border: none;
                border-bottom: 1px solid #d5dce5;
                padding: 6px 8px;
            }
            QScrollBar:vertical { background: #f3f5f8; width: 10px; }
            QScrollBar::handle:vertical { background: #c8d2de; border-radius: 5px; min-height: 30px; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        )");
        _themeToggleBtn->setText("深色");
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
    // 关闭顺序：先停渲染节拍 → 停分析线程 → 停事件落盘线程 → 停采集线程（自释放设备）
    if (_renderTimer)
        _renderTimer->stop();
    if (_analysisThread && _analysisThread->isRunning()) {
        _analysisThread->shutdown();
        _analysisThread->wait(2000);
    }
    if (_eventStore)
        _eventStore->shutdown();
    if (_captureThread && _captureThread->isRunning()) {
        _captureThread->stopCapture();
    }
    event->accept();
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (_forbiddenZoneBtn->isChecked() && event->button() == Qt::LeftButton) {
        // 仅在视频区域内开始绘制（D5：鼠标坐标属 MainWindow，须映射到 label 局部）
        if (!_videoLabel->geometry().contains(event->pos()))
            return;
        _drawingForbiddenZone = true;
        _zoneStart = event->pos();
        _zoneEnd = event->pos();
        const QPoint p = _videoLabel->mapFrom(this, _zoneStart);
        _rubberBand->setGeometry(QRect(p, p));
        _rubberBand->show();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (_drawingForbiddenZone) {
        _zoneEnd = event->pos();
        _rubberBand->setGeometry(QRect(
            _videoLabel->mapFrom(this, _zoneStart),
            _videoLabel->mapFrom(this, _zoneEnd)).normalized());
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (_drawingForbiddenZone && event->button() == Qt::LeftButton) {
        _drawingForbiddenZone = false;
        _forbiddenZoneBtn->setChecked(false);
        _rubberBand->hide();

        // D5：label 局部矩形 → 源帧坐标（分析/渲染/存证统一空间）
        const QRect labelRect = QRect(
            _videoLabel->mapFrom(this, _zoneStart),
            _videoLabel->mapFrom(this, _zoneEnd)).normalized();
        const cv::Rect zone = mapRectToFrame(labelRect);
        if (zone.empty()) {
            _logLabel->setText("禁区设置失败：无有效画面或选区过小");
            return;
        }
        _intrusionDetector->setForbiddenZone(zone);
        _logLabel->setText(QString("禁区设置（源帧坐标）: (%1,%2) %3x%4")
            .arg(zone.x).arg(zone.y).arg(zone.width).arg(zone.height));
    }
}

// label 坐标 → 源帧坐标（D5 修复核心）：
// updateVideoFrame 将源帧 KeepAspectRatio 缩放进 label，QLabel 默认居中显示，
// 因此 label 内像素 = 缩放后画面 + 居中偏移；反变换 = 减偏移再除缩放比
cv::Rect MainWindow::mapRectToFrame(const QRect& r) const
{
    const QPixmap* pmPtr = _videoLabel->pixmap();
    if (!pmPtr || pmPtr->isNull() || _lastFrame.empty())
        return cv::Rect();
    const QPixmap& pm = *pmPtr;

    const double sx = double(_lastFrame.cols) / pm.width();
    const double sy = double(_lastFrame.rows) / pm.height();
    const double offX = double(_videoLabel->width()  - pm.width())  / 2.0;
    const double offY = double(_videoLabel->height() - pm.height()) / 2.0;

    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    const int x1 = clampi(int((r.left()   - offX) * sx), 0, _lastFrame.cols - 1);
    const int y1 = clampi(int((r.top()    - offY) * sy), 0, _lastFrame.rows - 1);
    const int x2 = clampi(int((r.right() + 1 - offX) * sx), 0, _lastFrame.cols);
    const int y2 = clampi(int((r.bottom() + 1 - offY) * sy), 0, _lastFrame.rows);
    if (x2 - x1 < 8 || y2 - y1 < 8)   // 过小选区视为误触
        return cv::Rect();
    return cv::Rect(x1, y1, x2 - x1, y2 - y1);
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