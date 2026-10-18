// MainWindow.cpp
#include "MainWindow.h"
#include "core/dataConfig.h"
#include "ui/theme/ThemeManager.h"
#include "ui/TitleBar.h"

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

MainWindow::MainWindow(QWidget *parent) : FramelessWindow(parent)
{
    // 初始化设置（路径锚定 exe 目录，与启动 CWD 无关——见 DataConfig::appDir）
    _settings = new QSettings(DataConfig::settingsPath(), QSettings::IniFormat, this);
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
        // 跨线程 queued 信号 → GUI 线程写入；_resultMutex 保护 _hud 快照
        // （v0.4.0 diagnostics 由 MediaLink HTTP 线程读取，见 initMediaLink）
        QMutexLocker locker(&_resultMutex);
        _hud.captureFps = fps;
    });
    connect(_captureThread, &VideoCaptureThread::sourceStatus, this, [this](const QString& msg) {
        _logLabel->setText(msg);
    });

    // 分析线程：FrameBank 最新帧 → 识别 → 结构化结果（队列信号回 GUI）
    _analysisThread = new AnalysisThread(_frameBank, _faceRecognizer, _objectDetector, _intrusionDetector, this);
    connect(_analysisThread, &AnalysisThread::analysisReady, this, &MainWindow::onAnalysisReady);

    // 分析降采样（阶段02 性能项）：Analysis/scale 缺省 0.5 —— HAAR/MOG2 计算量 ∝ 像素数，
    // 0.5x ≈ 1/4 耗时；检测框坐标反缩放回源帧，minFace 同比缩小保持检测覆盖不变。
    // 置信度受轻微降采样影响时可在 ini 设 Analysis/scale=1.0 回退全分辨率
    const double analysisScale = _settings->value("Analysis/scale", 0.5).toDouble();
    _analysisThread->setAnalysisScale(analysisScale);
    _faceRecognizer->setMinFaceSize(qMax(24, qRound(60 * analysisScale)));
    _analysisThread->start(QThread::LowPriority);   // 低于 GUI 优先级，重计算不影响界面响应

    // 渲染节拍器：GUI 只做轻量绘制，60Hz 定时从 FrameBank 取最新帧
    // （版本去重：采集无新帧时不重绘，实际渲染帧率 = 采集帧率，见诊断角标）
    _renderTimer = new QTimer(this);
    connect(_renderTimer, &QTimer::timeout, this, &MainWindow::renderTick);
    _renderTimer->start(16);

    _renderClock.start();
    _captureThread->startCapture(loadSourceConfig());   // V0.6：源配置持久化恢复

    initMediaLink();   // 独立服务层（PRD 第 9 章）：HTTP 数据服务随启动开启
}

void MainWindow::renderTick()
{
    // 版本去重（阶段02 性能项）：采集无新帧时直接跳过——60Hz 节拍 × 30fps 相机的
    // 场景下，一半节拍是同一帧的重复克隆+绘制+缩放，纯烧 CPU 无视觉收益
    const int ver = _frameBank->version();
    if (ver == _lastRenderVersion)
        return;
    _lastRenderVersion = ver;

    cv::Mat frame = _frameBank->load();
    if (frame.empty())
        return;                 // 无新帧（暂停/断流）→ 保持上一画面
    _lastFrame = frame;         // 引用计数拷贝（只读，注册人脸用）

    // 服务层推流（PRD 第 9 章 / FR-06 流内容语义）：
    // 呈现帧模式（默认）：推"标注后帧"，网页画面与桌面一致（家用摄像头业界通行做法）
    // 原始帧模式：Stream/annotated=false 时推原始帧，叠加由客户端按结构化数据自绘（方案 B 前置）
    const bool pushAnnotated = _settings->value("Stream/annotated", true).toBool();
    if (!pushAnnotated && _streamOn && _mediaLink && !_lastFrame.empty())
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
    {
        QMutexLocker locker(&_resultMutex);   // diagnostics(HTTP线程) 同步读 _hud
        _hud.latencyMs = frameAgeMs;
        _hud.dropsPerSec = result.dropsPerSec;   // 帧覆盖率可见化（阶段02）
    }
    OverlayRenderer::paint(image, result.faces, result.objects, result.intrusion, zone, _hud);

    // 呈现帧模式推流（默认）：paint 后 draw 即标注帧，搭车喂给服务层
    if (pushAnnotated && _streamOn && _mediaLink)
        _mediaLink->pushFrame(draw.data, draw.cols, draw.rows, int(draw.step));

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

// 事件身份赋值单点（阶段02 / A5）：
//   ts = 帧采集时刻（result.captureTsMs 透传，事件↔画面精确回溯；无帧上下文退化为当前时刻）
//   seq = 同毫秒单调自增（仅 GUI 线程判定事件，天然无竞争；HTTP/WS/JSONL 三处同源）
// 边界：冷却/节流仍用挂钟 now，事件 ts 语义变化不影响冷却窗口
void MainWindow::stampEventIdentity(SecurityEvent& e)
{
    e.tsMs = e.captureTsMs > 0 ? e.captureTsMs : QDateTime::currentMSecsSinceEpoch();
    if (e.tsMs == _lastEvtTsMs) {
        e.seq = ++_lastEvtSeq;
    } else {
        _lastEvtTsMs = e.tsMs;
        _lastEvtSeq = 0;
        e.seq = 0;
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
                e.captureTsMs = result.captureTsMs;
                e.subject = f.name;
                e.similarity = f.similarity;
                e.detail = QStringLiteral("识别到已登记人员（相似度 %1%）").arg(f.similarity, 0, 'f', 0);
                stampEventIdentity(e);
                events.append(e);
            }
        } else if (f.similarity >= 0) {
            // 陌生人出现：有检测未命中；陌生人无稳定标识，按类型整体 30s 冷却
            if (_eventThrottle.allow(QStringLiteral("face_unknown"), now, 30 * 1000)) {
                SecurityEvent e;
                e.type = SecurityEvent::FaceUnknown;
                e.captureTsMs = result.captureTsMs;
                e.subject = QStringLiteral("陌生人");
                e.similarity = f.similarity;
                e.detail = QStringLiteral("出现未登记人脸（与档案最高相似度 %1%）").arg(f.similarity, 0, 'f', 0);
                stampEventIdentity(e);
                events.append(e);
            }
        }
    }

    if (result.intrusion.active) {
        // 禁区入侵：10s 冷却（R2）
        if (_eventThrottle.allow(QStringLiteral("intrusion:zone"), now, 10 * 1000)) {
            SecurityEvent e;
            e.type = SecurityEvent::Intrusion;
            e.captureTsMs = result.captureTsMs;
            e.subject = QStringLiteral("禁区");
            e.detail = QStringLiteral("有目标进入禁区（目标数 %1）").arg(result.intrusion.offenders.size());
            stampEventIdentity(e);
            events.append(e);
        }
    }

    // 遗留物沿检测：确认遗留出现 / 解除各触发一次（60s 保险冷却防抖动刷屏）
    const bool leftAny = !result.objects.empty();
    if (leftAny != _lastLeftAny) {
        if (_eventThrottle.allow(QStringLiteral("left_object"), now, 60 * 1000)) {
            SecurityEvent e;
            e.type = leftAny ? SecurityEvent::LeftObject : SecurityEvent::LeftCleared;
            e.captureTsMs = result.captureTsMs;
            e.subject = QStringLiteral("画面目标");
            e.detail = leftAny
                ? QStringLiteral("目标静止超过阈值，确认遗留")
                : QStringLiteral("遗留物已移走");
            stampEventIdentity(e);
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

    // ---- Mac 风自绘标题栏：红绿灯窗控 + 标题 + 状态 chips + 主题切换 ----
    _srcChip = new QLabel(this);              // 当前视频源（updateSourceChip 回显）
    _srcChip->setObjectName("srcChip");
    _svcChip = new QLabel(this);              // 服务层 HTTP/WS 状态（initMediaLink 回显）
    _svcChip->setObjectName("svcChip");
    _themeToggleBtn = new QPushButton(this);
    _themeToggleBtn->setObjectName("themeToggleBtn");
    connect(_themeToggleBtn, &QPushButton::clicked, this, &MainWindow::toggleTheme);

    _titleBar = new TitleBar(this);
    _titleBar->addRight(_srcChip);
    _titleBar->addRight(_svcChip);
    _titleBar->addRight(_themeToggleBtn);
    root->addWidget(_titleBar);

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

    // ---- 控制栏：Dock 药丸式（Mac 风），窗口底部居中 ----
    auto* btnBar = new QWidget(this);
    btnBar->setObjectName("dockBar");
    auto* btnLayout = new QHBoxLayout(btnBar);
    btnLayout->setContentsMargins(4, 4, 4, 4);
    btnLayout->setSpacing(2);

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
#ifdef Q_OS_WIN
            ThemeManager::instance().applyNative(&dlg);
#endif
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
        ThemeManager::instance().applyNative(&dlg);
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
            // v0.3.0：推流参数由 exe 旁 medialink.json 驱动（阶段03 收口），
            // 呈现帧含标注文字，码率上调保清晰度
            mlink::StreamConfig cfg = _mlCfg.toStreamConfig();
            cfg.bitrateKbps = 2500;
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

    // Dock 居中：左右 stretch 把药丸推到窗口中线
    auto* dockRow = new QHBoxLayout;
    dockRow->setContentsMargins(12, 2, 12, 10);
    dockRow->addStretch();
    dockRow->addWidget(btnBar);
    dockRow->addStretch();
    root->addLayout(dockRow);

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
    // Web 风格状态点：色点取语义令牌（ok 绿 / danger 红），描边色由 QSS state 驱动
    const QColor& dot = ok ? ThemeManager::instance().tokens().ok
                           : ThemeManager::instance().tokens().danger;
    chip->setText(QStringLiteral("<span style=\"color:%1;\">●</span>&nbsp;%2")
                      .arg(dot.name(), text));
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
    // v0.4.0 截图：relPath 即 EventStore::imagePath 语义（相对 eventsRoot，
    // 服务层已过路径安全校验）；存在才读，逐请求读盘——事件查询低频，无需缓存
    p.image = [this](const std::string& relPath, std::string& bytes) -> bool {
        const QString abs = _eventStore->resolveImagePath(QString::fromStdString(relPath));
        if (abs.isEmpty()) return false;
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray data = f.readAll();
        if (data.isEmpty()) return false;
        bytes.assign(data.constData(), size_t(data.size()));
        return true;
    };
    // v0.4.0 诊断指标：_hud 写在 GUI 线程、此回调在 HTTP 线程，经 _resultMutex 快照
    p.diagnostics = [this]() -> nlohmann::json {
        OverlayRenderer::Hud h;
        {
            QMutexLocker locker(&_resultMutex);
            h = _hud;
        }
        return {
            {"capture_fps", h.captureFps},
            {"analysis_fps", h.analysisFps},
            {"render_fps", h.renderFps},
            {"drops_per_sec", h.dropsPerSec},
            {"process_ms", h.processMs},
            {"latency_ms", h.latencyMs}
        };
    };
    _mediaLink->setProviders(p);

    // 端口配置（阶段03 收口）：单一来源 exe 旁 medialink.json；文件缺失时回退
    // FR-09 的 ini 覆盖（Server/httpPort，缺省 8080 契约锁定值）
    std::string cfgErr;
    const bool cfgOk = mlink::loadMediaLinkConfig("medialink.json", _mlCfg, cfgErr);
    if (!cfgOk)
        qWarning() << "MediaLink 配置缺失/非法，回退默认值：" << QString::fromStdString(cfgErr);
    int httpPort = _mlCfg.httpPort;
    int wsPort   = _mlCfg.wsPort;
    if (!cfgOk) {
        httpPort = _settings->value("Server/httpPort", 8080).toInt();
        wsPort   = _settings->value("Server/wsPort", 8081).toInt();
    }
    if (_mediaLink->startHttp(httpPort)) {
        const bool wsOk = _mediaLink->startWs(wsPort);
        setChipState(_svcChip, wsOk ? QStringLiteral("服务 :%1 · WS :%2").arg(httpPort).arg(wsPort)
                                    : QStringLiteral("服务 :%1 · WS 未启动").arg(httpPort), true);
        _logLabel->setText(QStringLiteral(
            "HTTP 数据服务 :%1 已开启（/api/health /api/events /api/persons /api/stream）%2")
            .arg(httpPort)
            .arg(wsOk ? QStringLiteral("；WS 事件推送 :%1 已开启").arg(wsPort)
                      : QStringLiteral("；WS :%1 端口被占用，事件推送未开启").arg(wsPort)));
    } else {
        setChipState(_svcChip, QStringLiteral("服务未启动"), false);
        _logLabel->setText(QStringLiteral("HTTP :%1 端口被占用，数据服务未开启").arg(httpPort));
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
    // 分辨率/帧率（阶段02 性能项）：60fps 需相机支持，驱动不支持时自动回落
    cfg.width  = _settings->value("Source/width",  640).toInt();
    cfg.height = _settings->value("Source/height", 480).toInt();
    cfg.fps    = _settings->value("Source/fps",     30).toInt();
    // 曝光（阶段02 实测项）：刚开启过曝 / 逆光低帧率时，可 ini 设
    // Camera/autoExposure=0 + Camera/exposure=-6（log2 秒，-6≈1/64s 保 30fps）
    cfg.autoExposure = _settings->value("Camera/autoExposure", true).toBool();
    cfg.exposure     = _settings->value("Camera/exposure", -6.0).toDouble();
    // 画质增强（阶段02 实测定论：默认关闭，裸帧+AE暖机即最优——见 VideoSource.h 注释）
    cfg.enhance     = _settings->value("Camera/enhance", false).toBool();
    cfg.gammaTarget = _settings->value("Camera/gammaTarget", 135).toDouble();
    cfg.blackLift   = _settings->value("Camera/blackLift", 14).toDouble();
    cfg.clipLimit   = _settings->value("Camera/clipLimit", 1.2).toDouble();
    cfg.saturation  = _settings->value("Camera/saturation", 1.10).toDouble();
    // 时域降噪（模拟驱动 3D NR）：Camera/nr=0 关闭
    cfg.noiseReduction = _settings->value("Camera/nr", true).toBool();
    cfg.nrAlpha        = _settings->value("Camera/nrAlpha", 0.15).toDouble();
    return cfg;
}

void MainWindow::saveSourceConfig(const VideoSourceConfig& cfg)
{
    _settings->setValue("Source/type", cfg.type == SourceType::RtspStream ? "rtsp" : "usb");
    _settings->setValue("Source/usbIndex", cfg.usbIndex);
    _settings->setValue("Source/url", cfg.url);
    _settings->setValue("Source/width",  cfg.width);
    _settings->setValue("Source/height", cfg.height);
    _settings->setValue("Source/fps",    cfg.fps);
    _settings->setValue("Camera/autoExposure", cfg.autoExposure);
    _settings->setValue("Camera/exposure",     cfg.exposure);
    _settings->setValue("Camera/enhance",      cfg.enhance);
    _settings->setValue("Camera/gammaTarget",  cfg.gammaTarget);
    _settings->setValue("Camera/blackLift",    cfg.blackLift);
    _settings->setValue("Camera/clipLimit",    cfg.clipLimit);
    _settings->setValue("Camera/saturation",   cfg.saturation);
    _settings->setValue("Camera/nr",           cfg.noiseReduction);
    _settings->setValue("Camera/nrAlpha",      cfg.nrAlpha);
    _settings->sync();
}

void MainWindow::showSourceDialog()
{
    const VideoSourceConfig cur = loadSourceConfig();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("切换视频源"));
    dlg.setMinimumWidth(360);
    ThemeManager::instance().applyNative(&dlg);
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

    // 分辨率/帧率（阶段02 性能项，仅 USB 源生效；RTSP 帧率由服务端决定）
    QComboBox* resBox = new QComboBox(&dlg);
    resBox->addItem(QStringLiteral("640 × 480"), QSize(640, 480));
    resBox->addItem(QStringLiteral("1280 × 720"), QSize(1280, 720));
    resBox->addItem(QStringLiteral("1920 × 1080"), QSize(1920, 1080));
    const QSize curRes(cur.width, cur.height);
    const int resIdx = [&]{ for (int i = 0; i < resBox->count(); ++i)
        if (resBox->itemData(i).toSize() == curRes) return i; return 0; }();
    resBox->setCurrentIndex(resIdx);

    QComboBox* fpsBox = new QComboBox(&dlg);
    fpsBox->addItem(QStringLiteral("30 fps"), 30);
    fpsBox->addItem(QStringLiteral("60 fps"), 60);
    const int fpsIdx = fpsBox->findData(cur.fps);
    fpsBox->setCurrentIndex(fpsIdx < 0 ? 0 : fpsIdx);
    auto syncUsbOnly = [typeBox, resBox, fpsBox](int idx) {
        resBox->setEnabled(idx != 2);
        fpsBox->setEnabled(idx != 2);
    };
    connect(typeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, syncUsbOnly);
    syncUsbOnly(typeBox->currentIndex());

    QDialogButtonBox* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    form->addRow(QStringLiteral("源类型"), typeBox);
    form->addRow(QStringLiteral("RTSP 地址"), urlEdit);
    form->addRow(QStringLiteral("分辨率（USB）"), resBox);
    form->addRow(QStringLiteral("帧率（USB）"), fpsBox);
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
    const QSize res = resBox->currentData().toSize();
    cfg.width = res.width(); cfg.height = res.height();
    cfg.fps = fpsBox->currentData().toInt();

    saveSourceConfig(cfg);
    updateSourceChip();           // 标题栏源 chip 同步
    if (_captureThread->switchSource(cfg))
        _logLabel->setText(QStringLiteral("视频源切换中…"));   // 后续状态经 sourceStatus 回显
    else
        _logLabel->setText(QStringLiteral("视频源切换失败：采集线程未就绪"));
}

void MainWindow::applyTheme(const QString &theme)
{
    // 主题切换委托 ThemeManager（设计令牌单点：QSS / 原生标题栏 / 语义色统一出处，
    // 详见 docs/UI设计规范.md；本函数只做宿主侧同步）
    ThemeManager::instance().apply(theme);

    const mvcore::uitheme::ThemeTokens& t = ThemeManager::instance().tokens();
    _currentTheme = t.name;
    _themeToggleBtn->setText(t.name == QStringLiteral("dark")
                                 ? QStringLiteral("浅色") : QStringLiteral("深色"));

    // Windows 原生标题栏跟随主题（DWM 深色模式 + Win11 精确配色，旧系统静默跳过）
    ThemeManager::instance().applyNative(this);
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