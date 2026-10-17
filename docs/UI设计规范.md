# SmartSurveillance UI 设计规范（Design Tokens v1.0）

> 生效范围：SmartSurveillance 桌面端全部界面（主窗口 / 弹窗 / 历史查询 / 视频叠加）
> 单点事实来源：`src/ui/theme/DesignTokens.h`（令牌）+ `src/ui/theme/ThemeManager.cpp`（QSS 模板）
> 运行时数据锚点：所有持久化路径（级联/档案/存证/参数）统一 `DataConfig::appDir()` = exe 目录，与启动 CWD 无关
> 本文档描述规范与用法；**改色值只改 DesignTokens.h，改规则只改 QSS 模板**，禁止在业务代码里写十六进制颜色。

---

## 1. 设计原则（B/S 风格的桌面落地）

| 原则 | 含义 | 落地手段 |
|---|---|---|
| 内容优先 | 视频是唯一主角，chrome（界面框架）退后 | 低饱和深色底、细 1px 分隔线、无重装饰 |
| 语义色克制 | 颜色即状态，不参与装饰 | 绿=正常/命中，琥珀=警示/绘制中，红=告警，蓝=焦点/选中 |
| 状态可预期 | 同类控件同套状态机 | 按钮 5 态（default/hover/pressed/disabled/on+checked）QSS 统一 |
| 单点事实来源 | 一处改，全局生效 | 令牌 → ThemeManager → QSS/原生标题栏/Overlay 三端同步 |
| 原生行为保留 | 不自绘窗口框架 | DWM 原生标题栏配色跟随主题（Win11 精确配色） |

## 2. 设计令牌

### 2.1 颜色令牌（语义分组，双主题同名单不同值）

| 令牌 | 深色 | 浅色 | 用途 |
|---|---|---|---|
| `bgBase` | `#151a21` | `#f3f5f8` | 窗口底 |
| `bgPanel` | `#1b222c` | `#ffffff` | 标题栏 / 弹窗 / 表头 |
| `bgElevated` | `#222b37` | `#ffffff` | 按钮 / 下拉弹层 |
| `bgHover` / `bgActive` | `#2a3543` / `#1f2833` | `#f0f4f9` / `#e6ecf3` | 悬停 / 按下 |
| `bgSunken` | `#10141a` | `#ffffff` | 状态栏 / 预览底 |
| `bgVideo` | `#060809` | `#060809` | 视频区（恒黑） |
| `bgInput` / `bgChip` | `#10141a` / `#202834` | `#ffffff` / `#f6f8fb` | 输入框 / 状态 chip |
| `border` → `borderStrong` → `borderHover` | `#262e3a` → `#2e3a49` → `#3d4c60` | `#e2e7ee` → `#d5dce5` → `#b9c4d2` | 弱分隔 → 控件描边 → 悬停描边 |
| `borderFocus` = `accent` | `#3b82f6` | `#3b82f6` | 焦点环 / 选中 |
| `textPrimary` | `#f0f4fa` | `#111827` | 标题 |
| `textSecondary` | `#dde3ec` | `#374151` | 正文 / 按钮 |
| `textMuted` | `#97a2b2` | `#5b6675` | 次要信息（chips/日志/表头） |
| `textDisabled` | `#5c6673` | `#a8b1bd` | 禁用 |
| `ok`（+okBg/okBorder） | `#4ade80` | `#15803d` | 开关 on 态 / chip 正常 |
| `warn`（+warnBg/warnBorder） | `#fbbf24` | `#b45309` | 禁区绘制 checked 态 |
| `danger`（+dangerBorder） | `#f87171` | `#b91c1c` | 服务异常 / 告警 |

### 2.2 视频叠加调色板 `OverlayPalette`（双主题共用——画面恒黑）

| 令牌 | 值 | 语义 |
|---|---|---|
| `knownFace` | `#22c55e` | 已知人脸角括号 + 标签 |
| `unknownFace` | `#eab308` | 陌生人角括号 + 标签 |
| `leftObject` | `#f59e0b` | 遗留物确认（加粗） |
| `trackBox` | `#9ca3af` | 跟踪中细框 |
| `zone` | `#ef4444` | 禁区虚线框 |
| `intrusion` / `banner` | `#dc2626` / 同色 α200 | 入侵粗框 / 顶部告警横幅 |
| `hudText` | `#34d399` | 诊断角标数值 |
| `labelScrim` | `#000000` α165 | 标签/角标半透明底 |

### 2.3 几何与字体刻度

- **圆角**：`RadiusSm=4`（tooltip/标签）· `RadiusMd=6`（按钮/输入框/表格）· `RadiusLg=8`（视频面板）· `RadiusPill=11`（状态 chips）
- **间距**：8px 网格 `4 / 8 / 12 / 16 / 24`；布局边距 12–14，控件内边距 6–8
- **字号**：`12`（chips/tooltip/主题切换）· `13`（正文基准）· `15`（应用标题）· `16`（告警横幅）
- **字体栈**：`Microsoft YaHei UI → Segoe UI → sans-serif`（中文优先）

## 3. 组件状态规范

### 3.1 按钮（QSS 驱动，业务代码只设 `state` 动态属性）

| 状态 | 视觉 | 业务触发 |
|---|---|---|
| default / hover / pressed | 中性底 + 描边渐亮 | 系统自动 |
| disabled | `textDisabled` + `bgBase` | 系统自动 |
| **on**（`state="on"`） | ok 绿描边 + okBg 淡绿底 | 开关型：人脸识别 / WebRTC 推流（`setToggleState`） |
| **checked** | warn 琥珀 | 互斥模式：禁区绘制中 |

规则：**on=持续生效的开关，checked=进入某种临时模式**，不得混用。

### 3.2 状态 chips（标题栏）

- 结构：`●色点 + 文本`，pill 圆角，`state` 属性驱动描边（ok→okBorder / bad→dangerBorder）
- 色点颜色取 `tokens().ok / danger`（`setChipState` 统一封装）
- 语义：源 chip（USB0/RTSP）恒 ok；服务 chip 反映 HTTP/WS 实际端口状态

### 3.3 视频叠加（OverlayRenderer，颜色只取 OverlayPalette）

| 元素 | 规范 |
|---|---|
| 检测框 | 四角括号（AI 视觉语言），角长 = min(w,h)/4 夹取 [10,36] |
| 标签 | 圆角 scrim 底 + 色点 + 文字，顶部空间不足移入框内 |
| 禁区 | 红虚线**全框**（边界闭合语义）+ 左上"禁区"标签 |
| 入侵 | 4px 红粗框 + 顶部**内缩圆角横幅**（margin 16 / 高 42 / 圆角 10，⚠ 前缀） |
| 诊断角标 | **右下角**（顶部让位给告警横幅/禁区标签）：预览fps · 分析Hz · 耗时 · 延迟 |

叠加层布局分区：顶部 = 告警横幅 + 禁区/人脸标签；底部右下 = 诊断角标——互不重叠。

## 4. 架构与扩展规则

```
DesignTokens.h ──> ThemeManager（单例，观察者：themeChanged 信号）
      │                  ├── qApp 全局 QSS（@token@ 参数化模板，双主题同规则）
      │                  └── applyNative()：DWM 原生标题栏同步（WinTheme.h 只收 COLORREF）
      └── OverlayPalette ──> OverlayRenderer（QPainter 叠加，与主题无关）

业务代码取色：ThemeManager::instance().tokens().ok 之类；禁止散落 QColor(0x..) 字面量
```

**新增主题**：在 DesignTokens.h 加一组令牌 + byName 分支即可，QSS 模板零改动。
**新增控件**：优先复用既有 selector 语义；确需新样式时加进 QSS 模板并引用令牌。
**新增语义色**：先问"能不能用 ok/warn/danger/accent 表达"，确属新语义才加令牌。

## 5. 文件映射

| 文件 | 职责 |
|---|---|
| `src/ui/theme/DesignTokens.h` | 全部令牌定义（颜色/圆角/间距/字号 + OverlayPalette） |
| `src/ui/theme/ThemeManager.h/.cpp` | 主题单例：apply/themeChanged/applyNative + QSS 模板 |
| `src/ui/WinTheme.h` | DWM 原生标题栏低层封装（只收 COLORREF，不持色值） |
| `src/ui/MainWindow.cpp` | 宿主侧同步：applyTheme 委托 / setChipState / 弹窗 applyNative |
| `src/ui/OverlayRenderer.cpp` | 叠加绘制（只取 OverlayPalette） |
