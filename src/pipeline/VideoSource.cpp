#include "pipeline/VideoSource.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtGlobal>
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
        _cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        _cap.set(cv::CAP_PROP_FRAME_WIDTH,  _cfg.width  > 0 ? _cfg.width  : 640);
        _cap.set(cv::CAP_PROP_FRAME_HEIGHT, _cfg.height > 0 ? _cfg.height : 480);
        _cap.set(cv::CAP_PROP_FPS,          _cfg.fps    > 0 ? _cfg.fps    : 30);
        _cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
        // 曝光控制（阶段02 实测项）：打开瞬间 AE 从高增益起点收敛 → 开头几秒过曝；
        // 逆光场景 AE 拉长曝光时间 → 帧率被压到个位数。DSHOW 后端 AUTO_EXPOSURE
        // 取菜单标志：0.75=自动连续，0.25=手动（再配 EXPOSURE=log2 秒）。
        // 驱动不支持某项时 set 返回 false 自动忽略，无副作用
        if (_cfg.autoExposure) {
            _cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.75);   // 重申自动：让 AE 从当前场景重新收敛
        } else {
            _cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.25);   // 手动：锁死快门（帧率与亮度双稳定）
            _cap.set(cv::CAP_PROP_EXPOSURE, _cfg.exposure);
        }
        qInfo() << "USB source opened:"
                << _cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x" << _cap.get(cv::CAP_PROP_FRAME_HEIGHT)
                << "fps=" << _cap.get(cv::CAP_PROP_FPS)
                << "autoExp=" << _cfg.autoExposure
                << "fourcc=" << QString::number(
                       static_cast<int>(_cap.get(cv::CAP_PROP_FOURCC)), 16);
        if (_cfg.autoExposure)
            warmUpAutoExposure();   // 冲掉 AE 收敛期的过曝帧（用户实测"刚开启过曝"）
        return true;
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
        return QStringLiteral("USB 摄像头 %1").arg(_cfg.usbIndex);
    }

private:
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
