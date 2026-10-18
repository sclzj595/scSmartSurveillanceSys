# SmartSurveillance RTSP 网络摄像头接入指南

| 文档属性 | 内容 |
|---|---|
| 文档版本 | v1.0 |
| 文档日期 | 2026-10-18 |
| 适用范围 | SmartSurveillance（V0.6+）视频源切换 / 阶段02 RTSP 真机验收 |
| 代码锚点 | [VideoSource.h](../src/pipeline/VideoSource.h) / [VideoSource.cpp](../src/pipeline/VideoSource.cpp)（RtspSource）/ [VideoCaptureThread.cpp](../src/pipeline/VideoCaptureThread.cpp)（D9 断流恢复）/ [MainWindow.cpp](../src/ui/MainWindow.cpp)（源配置 UI 与 ini） |
| 实现状态 | ✅ 代码侧完成（V0.6 统一源接口），**真机验收待做**（路线图阶段02） |

---

## 一、接入方式（二选一）

### 1.1 界面切换（推荐）

主界面 → **切换视频源** → 源类型选「RTSP 网络流」→ 填地址 → 应用：

```
rtsp://用户名:密码@192.168.1.64:554/stream1
```

地址为空时拒绝切换（红字提示）。切换即时生效：旧源关闭 → 新源异步打开（建连在采集线程，不卡 GUI）。

### 1.2 配置文件预置（免交互部署）

配置 ini（`DataConfig::settingsPath()` 锚定 exe 目录 data/ 下）`[Source]` 段：

```ini
[Source]
type=rtsp                      ; usb（默认）| rtsp
url=rtsp://admin:pass123@192.168.1.64:554/stream1
usbIndex=0                     ; 仅 USB 源生效
width=640                      ; 仅 USB 源生效（RTSP 忽略）
height=480                     ; 同上
fps=30                         ; 同上（RTSP 帧率由相机服务端决定）
```

> 注意：URL 含特殊字符（`@ : /`）时密码部分建议先 URL 编码（如 `p@ss` → `p%40ss`），FFmpeg 解析才不会错位。日志回显时会自动把 `://` 与 `@` 之间打码为 `***`，不会泄露账号密码。

## 二、常见相机 RTSP 地址速查

| 品牌 | 地址格式 | 备注 |
|---|---|---|
| Hikvision 海康 | `rtsp://user:pass@ip:554/Streaming/Channels/101` | 101=主码流（通道1子码流为102）；旧固件 `/h264/ch1/main/av_stream` |
| Dahua 大华 | `rtsp://user:pass@ip:554/cam/realmonitor?channel=1&subtype=0` | subtype=0 主码流 / 1 子码流 |
| Uniview 宇视 | `rtsp://user:pass@ip:554/media/video1` | video2=子码流 |
| TP-LINK / 水星 | `rtsp://user:pass@ipc_name/stream1` | 端口 554 可省略 |
| ONVIF 通用（W 源） | 相机 Web 后台「视频→取流」页直接可见 | 各家后台基本都展示标准 RTSP URL |
| 本机 MediaMTX 回环（联调用） | `rtsp://127.0.0.1:8554/sccam` | 套件推流链路自产流，先测链路再测真机 |

**建议接子码流**（如 101→102）：分析管线分辨率 640×480 量级足够，子码流码率低、丢帧少、CPU 降采样开销小；主码流 1080p 会白白放大解码成本。

## 三、传输与超时参数（代码已固化，无需配置）

RtspSource 打开时经 `OPENCV_FFMPEG_CAPTURE_OPTIONS` 注入两个 FFmpeg 参数（[VideoSource.cpp:284-293](../src/pipeline/VideoSource.cpp#L284-L293)）：

| 参数 | 值 | 作用 |
|---|---|---|
| `rtsp_transport` | `tcp` | **强制 RTSP over TCP**。UDP 下弱网丢包产生花屏绿块且不可恢复；TCP 重传保证帧完整性 |
| `stimeout` | `readTimeoutMs × 1000` = 3,000,000 µs（3 秒） | RTSP TCP socket I/O 超时，**建连与读帧共用**。断网/相机离线时 read 有界返回，采集线程不会挂在一次 read 上（否则 stopCapture 的 join 也救不回来） |

其余帧入口参数（USB 专用，RTSP 源全部忽略）：分辨率/帧率协商、曝光（autoExposure/exposure）、画质增强三段管线（gamma/CLAHE/饱和）、时域降噪（nr/nrAlpha）——这些都是 CAP_DSHOW 路径的 USB 相机参数。

重连退避（D9 断流恢复，USB/RTSP 统一路径，[VideoCaptureThread.cpp:99-118](../src/pipeline/VideoCaptureThread.cpp#L99-L118)）：

- 读帧失败 → close → 退避 500ms 起指数翻倍（500→1000→2000→4000→8000 封顶）→ 重开
- 重连成功退避复位；期间 UI 状态条实时显示「信号丢失，N ms 后重连…」
- 退避等待拆成 20ms 小片，保证程序退出时 stopCapture 能及时汇合

## 四、推流链路对 RTSP 源的兼容性

视频链路为「采集 → 分析 → FFmpeg 管道 → MediaMTX → WHEP/HLS」：

1. **源侧 RTSP 与推流侧 RTSP 是两条独立链路**。相机 RTSP 只作为采集输入，推流仍由 FFmpeg 重新编码后推给 MediaMTX（`rtsp://127.0.0.1:8554/sccam`），不存在级联端口冲突
2. MediaLink 的 `/api/stream` 返回的 whep_url/hls_url 按请求 Host 头动态生成，远端设备直接可播（与源无关）
3. **验收时别用回环流测真机项**：`rtsp://127.0.0.1:8554/sccam` 是自己推给自己的流，只能验证链路通，验证不了断流恢复——必须用真机相机拔网线测

## 五、真机验收矩阵（阶段02 退出条件，对应路线图 §六）

| 场景 | 操作 | 通过标准 |
|---|---|---|
| 正常接入 | 填地址应用 | 10s 内出画面，采集 FPS ≈ 相机子码流帧率 |
| 断网恢复 | 拔相机网线 30s → 插回 | UI 提示重连序列 → 恢复后画面/分析/推流三链路全部自动恢复，无需重启 |
| 相机重启 | 相机断电再上电 | 同上（RTSP 服务重启与网络断开是两种失败形态，都要测） |
| 认证失败 | 故意填错密码 | 打开失败反复退避重试、日志明确、UI 不卡死；纠正后恢复 |
| 持续断流 | 相机长期离线 | 退避封顶 8s 循环重试，CPU 占用无异常增长，其余功能（历史事件查询）不受影响 |
| 程序退出 | 断流状态下关闭程序 | 进程正常退出（stimeout 保证 read 有界，join 可汇合），不挂死 |
| 长稳 | 接入连续运行 24h | 资源无持续增长、延迟无漂移、无内存泄漏 → 打 v0.6.0 标签冻结 |

## 六、常见问题排查

| 现象 | 首查 | 次查 |
|---|---|---|
| 打开失败反复重试 | ping 相机 IP 通不通 | VLC 播 `rtsp://...` 能否出画面（VLC 能播而 SS 不能 → 报地址/固件；VLC 也不能 → 网络/认证） |
| 画面花屏绿块 | 确认是 TCP 传输（代码已强制，勿改 UDP） | 相机端码率上限调低 / 换子码流 |
| 延迟累积 | 分析线程是否过载（看诊断角标 drops_per_sec） | 降低子码流分辨率，分析管线跟不上时队列堆积 |
| 密码含 `@ : /` 连不上 | URL 编码特殊字符 | 或临时改无特殊字符密码隔离变量 |
| 554 端口通但打不开 | 相机 Web 后台确认 RTSP 服务已开启 | 海康部分新固件需勾选「RTSP 取流」 |
| USB/RTSP 切回 USB 后黑屏 | USB 相机被上次会话占用 → 拔插 | 见 project_memory：强杀程序可能致 Windows 相机管线异常 |
