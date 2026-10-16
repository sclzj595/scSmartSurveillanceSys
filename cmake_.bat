@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

:: 设置项目根目录（根据实际路径修改）
set "PROJECT_ROOT=D:\CODE_cpp\SmartSurveillance"

:: 1. 创建构建目录
echo 正在创建构建目录...
cd /d "%PROJECT_ROOT%"
if not exist "build" mkdir build

:: 2. 配置CMake项目
echo 正在配置CMake项目...
cd build
cmake -G "MinGW Makefiles" ^
  -DCMAKE_C_COMPILER="F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/gcc.exe" ^
  -DCMAKE_CXX_COMPILER="F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/g++.exe" ^
  -DCMAKE_MAKE_PROGRAM="F:/Apps/Qt5.15.2/Tools/mingw810_64/bin/mingw32-make.exe" ^
  ..

if %errorlevel% neq 0 (
    echo CMake配置失败!
    exit /b %errorlevel%
)

:: 3. 编译项目（使用6个线程）
echo 正在编译项目...
cmake --build . --parallel 6

if %errorlevel% neq 0 (
    echo 编译失败!
    exit /b %errorlevel%
)

:: 4. 运行程序（直接进入bin目录运行）
echo 正在运行程序...
cd bin
start SmartSurveillance.exe

echo 构建和运行完成!
endlocal