// VideoSource.h (V0.6 统一视频源抽象 / PRD 5.2 设备层)
#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include <QString>
#include <memory>
#include <opencv2/opencv.hpp>

// 统一视频源接口：源只做"单次 open / 单次 read / close"，自身无重连状态。
// 断流检测与退避重连统一由 VideoCaptureThread 编排（D9 修复）——
// USB / RTSP / 未来 ONVIF·文件回放等新源即插即得一致的恢复能力。
class IVideoSource
{
public:
    virtual ~IVideoSource() = default;

    virtual bool open() = 0;                 // 建立取流（可能耗时数秒，调用方须在工作线程）
    virtual bool read(cv::Mat& out) = 0;     // 阻塞取一帧；失败/空帧返回 false
    virtual void close() = 0;                // 释放设备（幂等）
    virtual bool isOpened() const = 0;
    virtual QString describe() const = 0;    // 人类可读标识（日志/状态栏用）
};

enum class SourceType
{
    UsbCamera,
    RtspStream,
};

struct VideoSourceConfig
{
    SourceType type = SourceType::UsbCamera;
    int usbIndex = 0;
    QString url;                     // rtsp://user:pass@ip:554/...
    int width  = 640;                // USB 采集分辨率/帧率（阶段02 性能项：可配置冲 60fps；
    int height = 480;                //  相机不支持时驱动自动回落，实际帧率见诊断角标"采集"）
    int fps    = 30;                 // RTSP 源忽略（帧率由服务端决定）
    int readTimeoutMs  = 3000;       // RTSP socket I/O 上界（建连/读帧共用）：断网时 read 有界返回，不挂线程
    int reconnectBaseMs = 500;       // D9 重连退避起点
    int reconnectMaxMs  = 8000;      // D9 重连退避上限

    bool operator==(const VideoSourceConfig& o) const
    {
        return type == o.type && usbIndex == o.usbIndex && url == o.url
            && width == o.width && height == o.height && fps == o.fps;
    }
    bool operator!=(const VideoSourceConfig& o) const { return !(*this == o); }
};

// 工厂：按配置构造对应源（后续 ONVIF / 移动相机 / 回放源在此扩展）
std::unique_ptr<IVideoSource> createVideoSource(const VideoSourceConfig& cfg);

#endif  // VIDEOSOURCE_H
