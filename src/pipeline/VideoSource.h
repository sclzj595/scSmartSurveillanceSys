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
    // 曝光控制（阶段02 实测项）：打开瞬间 AE 从高增益起点收敛 → 开头几秒过曝；
    // 逆光场景 AE 拉长曝光 → 帧率被压到 ~9fps。autoExposure=false 时锁死快门
    bool autoExposure = true;        // true=自动曝光+打开后暖机冲刷（等收敛再出画）
    double exposure = -6.0;          // 手动曝光（DSHOW log2 秒：-6≈1/64s，可保 30fps）
    // 画质增强（阶段02 实测项）：对齐系统相机观感 = lift-gamma-gain 调色曲线
    // （黑位抬起不死黑 + 中间调提亮）+ 轻度 CLAHE + 饱和补偿
    bool enhance = true;             // USB 源读帧后逐帧增强（全管线约 6ms@640x480）
    double gammaTarget = 135;        // 自适应 gamma 目标均值 0-255：0=关闭；只提亮不压暗
    double blackLift = 14;           // 黑位抬升 0-64：暗部不再压成死黑，背景细节显现（系统相机感的核心）
    double clipLimit = 1.2;          // CLAHE 强度（轻度）：>1 生效，防噪声爆炸
    double saturation = 1.10;        // 饱和补偿倍数：提亮后色彩回鲜；1.0=不动
    // 时域降噪（模拟系统相机驱动 3D NR）：实测代价过高默认关闭——
    // 浮点掩膜全套 ~20ms 拖垮采集帧率（30→14），运动区掩膜不全导致"融化"鬼影。
    // 实时性优先（用户明确要求）：保留代码备查，弱噪声场景可手动开启
    bool noiseReduction = false;     // 静止区时域平均 + 色斑清理；运动区直通防拖影
    double nrAlpha = 0.15;           // 静止区收敛系数：越小越干净（拖影风险越高）

    bool operator==(const VideoSourceConfig& o) const
    {
        return type == o.type && usbIndex == o.usbIndex && url == o.url
            && width == o.width && height == o.height && fps == o.fps
            && autoExposure == o.autoExposure && exposure == o.exposure
            && enhance == o.enhance && gammaTarget == o.gammaTarget
            && blackLift == o.blackLift && clipLimit == o.clipLimit
            && saturation == o.saturation && noiseReduction == o.noiseReduction
            && nrAlpha == o.nrAlpha;
    }
    bool operator!=(const VideoSourceConfig& o) const { return !(*this == o); }
};

// 工厂：按配置构造对应源（后续 ONVIF / 移动相机 / 回放源在此扩展）
std::unique_ptr<IVideoSource> createVideoSource(const VideoSourceConfig& cfg);

#endif  // VIDEOSOURCE_H
