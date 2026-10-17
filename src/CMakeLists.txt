# 模块化源码结构（依赖单向：ui → {pipeline, vision, events} → core）
#   ui/       Qt 表现层：主窗口/标注渲染/历史对话框/原生标题栏主题
#   pipeline/ 异步识别管线：FrameBank 最新帧槽 + 采集/分析线程 + 统一视频源（FR-01, V0.6）
#   vision/   识别引擎适配器：人脸/遗留物/禁区（业务判定与算法翻译）
#   events/   事件持久化：events.jsonl + 截图存证（PRD 第 8 章）
#   core/     领域类型与配置：AnalysisResult / dataConfig（零依赖，被所有层引用）
set(APP_SOURCES
    # ui —— 表现层
    ui/main.cpp
    ui/MainWindow.h             ui/MainWindow.cpp
    ui/OverlayRenderer.h        ui/OverlayRenderer.cpp
    ui/EventHistoryDialog.h     ui/EventHistoryDialog.cpp
    ui/WinTheme.h
    ui/FramelessWindow.h       # 无边框窗基类：NCCALCSIZE 剥框架 + 缩放热区
    ui/TitleBar.h              ui/TitleBar.cpp             # Mac 风自绘标题栏（红绿灯）
    ui/theme/DesignTokens.h     # 设计令牌（颜色/圆角/间距刻度单点）
    ui/theme/ThemeManager.h     ui/theme/ThemeManager.cpp   # 主题单点：令牌→QSS/原生标题栏
    # pipeline —— 异步识别管线（FrameBank 已提炼至 mvcore 共享库，见根 CMakeLists）
    pipeline/VideoSource.h      pipeline/VideoSource.cpp
    pipeline/VideoCaptureThread.h pipeline/VideoCaptureThread.cpp
    pipeline/AnalysisThread.h   pipeline/AnalysisThread.cpp
    # vision —— 识别引擎适配器
    vision/FaceRecognition.h    vision/FaceRecognition.cpp
    vision/ObjectDetection.h    vision/ObjectDetection.cpp
    vision/IntrusionDetection.h vision/IntrusionDetection.cpp
    # events —— 事件持久化
    events/EventStore.h         events/EventStore.cpp
    # core —— 领域类型与配置
    core/dataConfig.h
    core/AnalysisTypes.h
)

# 添加可执行目标（显式清单：新增文件必须显式登记，防止 GLOB 隐式收编漏 moc）
add_executable(SmartSurveillance ${APP_SOURCES})

# 头文件查找根 = src/（模块前缀 include：#include "pipeline/FrameBank.h"）
target_include_directories(SmartSurveillance PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}  # 用于自动生成的MOC文件
)

# 链接库
target_link_libraries(SmartSurveillance
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Concurrent
    ${OpenCV_LIBS}
    mvcore            # 套件共享基础库：FrameBank/PathResolver/EventId（header-only）
    FaceAlgorithm      # 可插拔人脸算法库 DLL（同一构建树，产物自动落 build/bin）
    medialink          # 独立服务层：HTTP 数据服务 + WebRTC 推流（PRD 第 9 章）
)
if(WIN32)
  target_link_libraries(SmartSurveillance dwmapi)   # 原生标题栏深色主题（WinTheme.h）
endif()

# 设置目标属性 - 确保在build/bin下
set_target_properties(SmartSurveillance PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 在Windows上复制必要的DLL到build/bin目录
if(WIN32)
  # 复制OpenCV DLL
  add_custom_command(TARGET SmartSurveillance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/utils/opencv4_5_2/x64/mingw/bin/"
      $<TARGET_FILE_DIR:SmartSurveillance>
    COMMENT "Copying OpenCV DLLs to output directory"
  )
  
  # 复制Qt核心DLL
  add_custom_command(TARGET SmartSurveillance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/Qt5Core.dll"
      $<TARGET_FILE_DIR:SmartSurveillance>
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/Qt5Gui.dll"
      $<TARGET_FILE_DIR:SmartSurveillance>
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/Qt5Widgets.dll"
      $<TARGET_FILE_DIR:SmartSurveillance>
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/Qt5Concurrent.dll"
      $<TARGET_FILE_DIR:SmartSurveillance>
    COMMENT "Copying Qt DLLs to output directory"
  )
  
  # 复制 MinGW 运行时三件套（必须钉死 gcc8.1 时代版本）
  # OpenCV 4.5.2 mingw 版依赖老 ABI 符号（fpos<int>，如 _ZNSi5seekgESt4fposIiE），
  # gcc9+ 的 libstdc++-6.dll 已改为 fpos<mbstate_t> 不再导出老符号——
  # 若 PATH 先命中新版 libstdc++ 会报"无法定位程序输入点"，故必须随 exe 分发
  add_custom_command(TARGET SmartSurveillance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/libstdc++-6.dll"
      $<TARGET_FILE_DIR:SmartSurveillance>
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/libgcc_s_seh-1.dll"
      $<TARGET_FILE_DIR:SmartSurveillance>
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/bin/libwinpthread-1.dll"
      $<TARGET_FILE_DIR:SmartSurveillance>
    COMMENT "Copying MinGW runtime DLLs (pinned to gcc8.1 ABI)"
  )

  # 复制Qt平台插件
  add_custom_command(TARGET SmartSurveillance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:SmartSurveillance>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${Qt5_ROOT}/plugins/platforms/qwindows.dll"
      "$<TARGET_FILE_DIR:SmartSurveillance>/platforms"
    COMMENT "Copying Qt platform plugin"
  )
  
  # 复制OpenGL支持文件（如果需要）
  # add_custom_command(TARGET SmartSurveillance POST_BUILD
  #   COMMAND ${CMAKE_COMMAND} -E copy
  #     "${Qt5_ROOT}/bin/libEGL.dll"
  #     $<TARGET_FILE_DIR:SmartSurveillance>
  #   COMMAND ${CMAKE_COMMAND} -E copy
  #     "${Qt5_ROOT}/bin/libGLESv2.dll"
  #     $<TARGET_FILE_DIR:SmartSurveillance>
  #   COMMENT "Copying OpenGL support DLLs"
  # )
endif()