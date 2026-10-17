cmake_minimum_required(VERSION 3.15)
project(SmartSurveillance)

# 默认 Release（MinGW Makefiles 留空 = -O0，CV 像素循环慢 3-5 倍，会直接导致识别卡顿）
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 设置输出目录为build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 启用自动MOC和RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_COMPILER "F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/g++.exe")
set(CMAKE_MAKE_PROGRAM "F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/mingw32-make.exe")

# 手动指定Qt5路径
set(Qt5_ROOT "F:/Apps/Qt5.15.2/5.15.2/mingw81_64")
set(CMAKE_PREFIX_PATH "${Qt5_ROOT}" ${CMAKE_PREFIX_PATH})

# 手动指定OpenCV路径
set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/utils/opencv4_5_2/x64/mingw/lib")

# 查找Qt组件
find_package(Qt5 COMPONENTS Core Gui Widgets Concurrent REQUIRED)

# 查找OpenCV
find_package(OpenCV REQUIRED
  COMPONENTS core imgproc video videoio objdetect)

# 可插拔人脸算法库（独立 DLL，业务无关；与人脸闸机项目共用同一份算法源码）
set(FACEALGORITHM_DIR "${CMAKE_SOURCE_DIR}/../FaceAlgorithm" CACHE PATH "FaceAlgorithm 源码目录")
add_subdirectory(${FACEALGORITHM_DIR} ${CMAKE_BINARY_DIR}/FaceAlgorithm)

# 添加子目录
add_subdirectory(src)

# 部署资源文件到bin目录
file(COPY resources/ DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# 验证配置
message(STATUS "Qt5 version: ${Qt5_VERSION}")
message(STATUS "OpenCV found: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# 安装目标
install(TARGETS SmartSurveillance
  RUNTIME DESTINATION bin
  BUNDLE DESTINATION bin
)