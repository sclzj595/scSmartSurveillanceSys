#include "pipeline/VideoSource.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtGlobal>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace {

// USB 本地摄像头：DirectShow 后端（Windows 下枚举最快、热插拔响应好）
class UsbCameraSource : public IVideoSource
{
public:
    explicit UsbCameraSource(const VideoSourceConfig& cfg) : _cfg(cfg) {}

    bool open() override
    {
        close();
        if (!_cap.open(_cfg.usbIndex, cv::CAP_DSHOW))
            return false;
        // DSHOW 缺省协商在部分驱动状态下会落到"黑帧 + ~1fps"坏模式
        // （实测：同一摄像头 ffmpeg 直取 15fps 正常，OpenCV 缺省协商却 1fps 黑帧）。
        // 显式重协商 MJPG + 分辨率/帧率 + 单帧缓冲绕开坏路径；
        // 相机不支持某项时 set 返回 false 自动维持缺省，无副作用。
        // 分辨率/帧率来自源配置（阶段02 性能项）：60fps 需相机支持，不支持时驱动回落
        // 曝光控制（阶段02 实测项）：打开瞬间 AE 从高增益起点收敛 → 开头几秒过曝；
        // 逆光场景 AE 拉长曝光时间 → 帧率被压到个位数。DSHOW 后端 AUTO_EXPOSURE
        // 取菜单标志：0.75=自动连续，0.25=手动（再配 EXPOSURE=log2 秒）。
        // 驱动不支持某项时 set 返回 false 自动忽略，无副作用
        // ⚠️ 必须先设曝光再设 FOURCC：DSHOW 下设置曝光属性会触发 capture pin
        // 重连，媒体类型回退驱动缺省 YUY2（实测 fourcc 变 "YUY2"、~1fps 卡顿）——
        // 顺序颠倒就是"加载特别慢 + Capture FPS: 0"的根因
        if (_cfg.autoExposure) {
            _cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.75);   // 重申自动：让 AE 从当前场景重新收敛
        } else {
            _cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.25);   // 手动：锁死快门（帧率与亮度双稳定）
            _cap.set(cv::CAP_PROP_EXPOSURE, _cfg.exposure);
        }
        _cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        _cap.set(cv::CAP_PROP_FRAME_WIDTH,  _cfg.width  > 0 ? _cfg.width  : 640);
        _cap.set(cv::CAP_PROP_FRAME_HEIGHT, _cfg.height > 0 ? _cfg.height : 480);
        _cap.set(cv::CAP_PROP_FPS,          _cfg.fps    > 0 ? _cfg.fps    : 30);
        _cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
        // 画质增强器（三段组合）：读帧路径逐帧应用，预览/分析/推流同帧受益
        _clahe.release();
        if (_cfg.enhance && _cfg.clipLimit > 1.0)
            _clahe = cv::createCLAHE(_cfg.clipLimit, cv::Size(8, 8));
        // 时域降噪基准复位（重开流 = 场景可能已变，重新收敛）
        _nrAcc.release();
        _nrPrevGray.release();
        // 调色曲线缓存同步复位（场景变了，旧均值参考无效）
        _gammaLut.release();
        _lutGamma = _lutLift = -1;
        _lastMean = -1;
        // 饱和补偿 LUT：HSV 的 S 通道整体乘系数（提亮后色彩回鲜），开器时建一次
        _satLut.release();
        if (_cfg.enhance && _cfg.saturation > 1.001) {
            cv::Mat lut(1, 256, CV_8UC1);
            for (int i = 0; i < 256; ++i)
                lut.at<uchar>(i) = cv::saturate_cast<uchar>(i * _cfg.saturation);
            _satLut = lut;
        }
        // 协商结果核验：fourcc 转可读字符，非 MJPG 立即告警（YUY2 → ~1fps 教训）
        {
            const qint64 fcc = static_cast<qint64>(_cap.get(cv::CAP_PROP_FOURCC));
            char fccStr[5] = { char(fcc & 0xFF), char((fcc >> 8) & 0xFF),
                               char((fcc >> 16) & 0xFF), char((fcc >> 24) & 0xFF), 0 };
            if (qstrncmp(fccStr, "MJPG", 4) != 0)
                qWarning() << "[VideoSource] MJPG negotiation FAILED, got" << fccStr
                           << "— expect very low fps on USB2 bandwidth!";
            qInfo() << "USB source opened:"
                    << _cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x" << _cap.get(cv::CAP_PROP_FRAME_HEIGHT)
                    << "fps=" << _cap.get(cv::CAP_PROP_FPS)
                    << "autoExp=" << _cfg.autoExposure
                    << "fourcc=" << fccStr;
        }
        if (_cfg.autoExposure)
            warmUpAutoExposure();   // 冲掉 AE 收敛期的过曝帧（用户实测"刚开启过曝"）
        return true;
    }
    bool read(cv::Mat& out) override
    {
        _cap >> out;
        if (out.empty())
            return false;
        // 画质管线（降噪+调色）整体兜底：任何 OpenCV 异常退化为直通原始帧，
        // 采集线程绝不因图像处理挂掉（闪退教训：threshold 不支持多通道）
        try {
            // 降噪在调色之前：先消时域噪声，后续提亮曲线才不会把噪声一起放大
            if (_cfg.noiseReduction) {
                applyTemporalNr(out);
                smoothChroma(out);
            }
            if (_cfg.enhance)
                applyEnhance(out);
        } catch (const cv::Exception& e) {
            qWarning() << "[VideoSource] image pipeline error, raw passthrough:"
                       << e.what();
        }
        return true;
    }
    void close() override
    {
        if (_cap.isOpened())
            _cap.release();
    }
    bool isOpened() const override { return _cap.isOpened(); }
    QString describe() const override
    {
        return QStringLiteral("USB 摄像头 %1").arg(_cfg.usbIndex);
    }

private:
    // ---- 三段增强管线（顺序有讲究：先整体提亮再局部均衡，最后色彩回鲜）----
    void applyEnhance(cv::Mat& out)
    {
        // 1) lift-gamma-gain 调色曲线（系统相机观感的核心）：
        //    lut[i] = lift + (255-lift) * (i/255)^gamma
        //    —— 黑位抬起（暗部成深灰不死黑，背景细节显现）+ gamma 拉中间调；
        //    gamma 按全帧均值自适应到目标亮度，只提亮不压暗
        //    ⚠️ LUT 缓存：均值波动 < 3 时复用上一帧曲线。逐帧重算会让输出
        //    亮度帧间抖动 → MOG2 背景模型整体失效 → 整帧被当前景（实测
        //    "New object detected 0,0 320x240" 刷屏）
        if (_cfg.gammaTarget > 0 || _cfg.blackLift > 0.5) {
            const cv::Scalar s = cv::mean(out);
            const double mean = (s[0] + s[1] + s[2]) / 3.0;
            const bool cfgStable = qFuzzyCompare(_lutGamma, _cfg.gammaTarget)
                                && qFuzzyCompare(_lutLift, _cfg.blackLift);
            if (cfgStable && !_gammaLut.empty()
                && _lastMean > 0 && qAbs(mean - _lastMean) < 3.0) {
                cv::LUT(out, _gammaLut, out);   // 复用缓存曲线
                _lastMean = mean;               // 均值随光照缓变，跨出 ±3 带才重算
            } else {
                double gamma = 1.0;
                if (_cfg.gammaTarget > 0 && mean > 1.0 && mean < _cfg.gammaTarget) {
                    gamma = std::log(_cfg.gammaTarget / 255.0) / std::log(mean / 255.0);
                    gamma = qBound(0.70, gamma, 1.0);
                }
                const double lift = qBound(0.0, _cfg.blackLift, 64.0);
                if (gamma < 0.995 || lift > 0.5) {
                    cv::Mat lut(1, 256, CV_8UC1);
                    for (int i = 0; i < 256; ++i) {
                        const double v = lift + (255.0 - lift)
                                       * std::pow(i / 255.0, gamma);
                        lut.at<uchar>(i) = cv::saturate_cast<uchar>(v);
                    }
                    cv::LUT(out, lut, out);
                    _gammaLut = lut;            // 记住本帧曲线与均值
                    _lutGamma = _cfg.gammaTarget;
                    _lutLift   = lift;
                } else {
                    _gammaLut.release();        // 曲线退化为恒等 → 清缓存
                    _lutLift = 0;
                }
                _lastMean = mean;
            }
        }
        // 2) 轻度 CLAHE：只调 LAB 亮度通道，补局部小反差（暗角里的脸），主亮度不靠它
        if (_clahe) {
            cv::Mat lab;
            cv::cvtColor(out, lab, cv::COLOR_BGR2Lab);
            std::vector<cv::Mat> ch;
            cv::split(lab, ch);
            _clahe->apply(ch[0], ch[0]);
            cv::merge(ch, lab);
            cv::cvtColor(lab, out, cv::COLOR_Lab2BGR);
        }
        // 3) 饱和补偿：gamma+CLAHE 提亮会让色彩观感变淡，S 通道乘系数回鲜
        if (!_satLut.empty()) {
            cv::Mat hsv;
            cv::cvtColor(out, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> ch;
            cv::split(hsv, ch);
            cv::LUT(ch[1], _satLut, ch[1]);
            cv::merge(ch, hsv);
            cv::cvtColor(hsv, out, cv::COLOR_HSV2BGR);
        }
    }

    // ---- 时域降噪（对齐系统相机驱动里的 3D NR）----
    // 原理：暗场噪声是逐帧随机的，多帧指数滑动平均可将其约 √N 倍地压掉；
    // 运动掩膜保护移动目标（变化超阈值的像素走直通），静止区重度平均。
    // 这就是"系统相机周围没噪点"的技术来源——驱动 3D NR 的软件等价物
    void applyTemporalNr(cv::Mat& frame)
    {
        cv::Mat f32;
        frame.convertTo(f32, CV_32F);
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        if (_nrAcc.empty()) {           // 首帧：建立基准
            f32.copyTo(_nrAcc);
            _nrPrevGray = gray.clone();
            return;
        }
        // 运动掩膜（必须全程单通道：threshold 不支持多通道）：
        // 灰度差 > 28 判为运动 → 直通（alpha=1）防拖影；5x5 羽化防交界闪烁
        cv::Mat diff, motion;
        cv::absdiff(gray, _nrPrevGray, diff);
        cv::threshold(diff, motion, 28.0, 255.0, cv::THRESH_BINARY);
        cv::boxFilter(motion, motion, -1, cv::Size(5, 5));
        cv::threshold(motion, motion, 20.0, 255.0, cv::THRESH_BINARY);
        cv::Mat alpha;
        motion.convertTo(alpha, CV_32F, 1.0 / 255.0);
        const cv::Mat base(motion.size(), CV_32F,
                           cv::Scalar(qBound(0.05, _cfg.nrAlpha, 0.5)));
        cv::max(base, alpha, alpha);    // 逐像素：静止→nrAlpha，运动→1.0
        cv::Mat alpha3;
        cv::cvtColor(alpha, alpha3, cv::COLOR_GRAY2BGR);   // 广播到 3 通道
        cv::Mat delta;
        cv::multiply(alpha3, f32 - _nrAcc, delta);
        cv::add(_nrAcc, delta, _nrAcc);
        _nrAcc.convertTo(frame, CV_8U);
        _nrPrevGray = gray.clone();
    }

    // ---- 色斑清理 ----
    // MJPG 4:2:0 压缩的椒盐色斑集中在色度（低频），色度 2x 降采样再复原即除，
    // 亮度通道不动（细节无损）；这就是背景红绿噪点的来源
    void smoothChroma(cv::Mat& frame)
    {
        cv::Mat ycc;
        cv::cvtColor(frame, ycc, cv::COLOR_BGR2YCrCb);
        std::vector<cv::Mat> ch;
        cv::split(ycc, ch);
        cv::resize(ch[1], ch[1], cv::Size(), 0.5, 0.5, cv::INTER_AREA);
        cv::resize(ch[2], ch[2], cv::Size(), 0.5, 0.5, cv::INTER_AREA);
        cv::resize(ch[1], ch[1], ycc.size(), 0, 0, cv::INTER_LINEAR);
        cv::resize(ch[2], ch[2], ycc.size(), 0, 0, cv::INTER_LINEAR);
        cv::merge(ch, ycc);
        cv::cvtColor(ycc, frame, cv::COLOR_YCrCb2BGR);
    }

    // AE 暖机冲刷：打开后丢弃帧直到亮度稳定（连续两帧均值差 < 2.5），
    // 有界 ~1.2s / 30 帧——收敛完成前不对外出画，首帧即正常曝光。
    // 亮度用 64x64 缩略图均值，单次开销微秒级，不拖慢打开路径
    void warmUpAutoExposure()
    {
        QElapsedTimer timer;
        timer.start();
        cv::Mat f, small, gray;
        double prevMean = -1.0;
        int stable = 0, discarded = 0;
        while (discarded < 30 && timer.elapsed() < 1200) {
            _cap >> f;
            if (f.empty()) break;
            ++discarded;
            cv::resize(f, small, cv::Size(64, 64), 0, 0, cv::INTER_AREA);
            cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
            const double mean = cv::mean(gray)[0];
            if (prevMean >= 0 && qAbs(mean - prevMean) < 2.5) {
                if (++stable >= 2) break;   // 连续 3 帧亮度稳定 → AE 已收敛
            } else {
                stable = 0;
            }
            prevMean = mean;
        }
        qInfo() << "AE warm-up: discarded" << discarded << "frames in" << timer.elapsed() << "ms";
    }

    VideoSourceConfig _cfg;
    cv::VideoCapture _cap;
    cv::Ptr<cv::CLAHE> _clahe;   // 轻度局部均衡（空 = 关闭）
    cv::Mat _satLut;             // 饱和补偿 LUT（空 = 不补偿）
    cv::Mat _nrAcc;              // 时域降噪累加器（CV_32F）
    cv::Mat _nrPrevGray;         // 上一帧灰度（运动掩膜参考，单通道）
    cv::Mat _gammaLut;           // 调色曲线缓存（帧间稳定，防 MOG2 整帧误检）
    double _lutGamma = -1;       // 缓存曲线对应的 gammaTarget 配置值
    double _lutLift = -1;        // 缓存曲线对应的实际 lift
    double _lastMean = -1;       // 上一帧全帧均值（±3 复用带）
};

// RTSP 网络流：FFMPEG 后端 + socket 超时。
// 网络相机离线或断网时，open/read 必须有界返回，否则采集线程会挂在
// 一次 read 上，stopCapture 的 join 也救不回来（D9 的 RTSP 变体）。
// 注：本份 OpenCV 4.5.2 无 CAP_PROP_OPEN/READ_TIMEOUT_MSEC，
// 改用 FFmpeg 原生 stimeout（µs，RTSP TCP socket I/O 超时）经
// OPENCV_FFMPEG_CAPTURE_OPTIONS 注入，建连与读帧共用该上界。
class RtspSource : public IVideoSource
{
public:
    explicit RtspSource(const VideoSourceConfig& cfg) : _cfg(cfg) {}

    bool open() override
    {
        close();
        // RTSP over TCP：UDP 丢包产生花屏块，弱网下 TCP 更稳。
        // 环境变量进程内一次性生效，重复 set 幂等；仅 RTSP 打开路径会读它。
        const QString opts = QStringLiteral(
            "rtsp_transport;tcp|stimeout;%1").arg(_cfg.readTimeoutMs * 1000);
        qputenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", opts.toUtf8());
        return _cap.open(_cfg.url.toStdString(), cv::CAP_FFMPEG);
    }
    bool read(cv::Mat& out) override { _cap >> out; return !out.empty(); }
    void close() override
    {
        if (_cap.isOpened())
            _cap.release();
    }
    bool isOpened() const override { return _cap.isOpened(); }
    QString describe() const override
    {
        // 日志里不回显 URL 内嵌的账号密码
        const int schemeEnd = _cfg.url.indexOf(QLatin1String("://"));
        const int at = _cfg.url.lastIndexOf('@');
        QString shown = _cfg.url;
        if (schemeEnd > 0 && at > schemeEnd)
            shown = _cfg.url.left(schemeEnd + 3) + QStringLiteral("***") + _cfg.url.mid(at);
        return QStringLiteral("RTSP 流 %1").arg(shown);
    }

private:
    VideoSourceConfig _cfg;
    cv::VideoCapture _cap;
};

}  // namespace

std::unique_ptr<IVideoSource> createVideoSource(const VideoSourceConfig& cfg)
{
    switch (cfg.type) {
    case SourceType::RtspStream:
        if (cfg.url.trimmed().isEmpty()) {
            qWarning() << "RTSP source config has empty url";
            return nullptr;
        }
        return std::make_unique<RtspSource>(cfg);
    case SourceType::UsbCamera:
    default:
        return std::make_unique<UsbCameraSource>(cfg);
    }
}
