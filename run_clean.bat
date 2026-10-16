@echo off
setlocal
set ORIG_PATH=%PATH%

:: 清空 PATH 只保留必要项
set PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem

:: 添加您的程序所需路径
set PATH=%PATH%;F:\Apps\Qt5.15.2\5.15.2\mingw81_64\bin
set PATH=%PATH%;F:\CODE\Qt_forNoVS\SmartSurveillance\utils\opencv4_5_2\x64\mingw\bin
set PATH=%PATH%;F:\Apps\Qt5.15.2\Tools\mingw810_64\bin

:: 设置插件路径
set QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0platforms

:: 运行程序
cd /d %~dp0
SmartSurveillance.exe

:: 恢复原始 PATH
endlocal
set PATH=%ORIG_PATH%