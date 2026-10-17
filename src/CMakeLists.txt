# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}  # 用于自动生成的MOC文件
)

# 收集源文件
file(GLOB SRCS 
    "app/*.cpp"
    "app/*.h" 
    "include/*.h"
)

# 添加可执行目标
add_executable(SmartSurveillance ${SRCS})

# 链接库
target_link_libraries(SmartSurveillance
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Concurrent
    ${OpenCV_LIBS}
    FaceAlgorithm      # 可插拔人脸算法库 DLL（同一构建树，产物自动落 build/bin）
)

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