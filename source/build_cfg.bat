@echo off
setlocal
rem   ===================================================================
rem   build_cfg.bat - BorderLimited 配置 GUI 编译脚本 (单文件 exe)
rem
rem   编译输出:
rem     source\build\BorderLimitedConfig.exe   (约 320KB, 无外部依赖)
rem
rem   依赖:
rem     - Visual Studio 2019+ (MSVC) + Windows SDK
rem
rem   用法:
rem     在 source 目录下双击运行, 或命令行执行 source\build_cfg.bat
rem
rem   说明:
rem     所有路径都从本脚本自身位置推导, 仓库整体移动/改名后无需修改本文件
rem   ===================================================================

rem   ---- 目录推导 ----
rem     SRC_DIR = <repo>\source       (%~dp0 就是 source 目录)
for %%I in ("%~dp0.") do set "SRC_DIR=%%~fI"

set "GUI_SRC=%SRC_DIR%\src\BorderLimitedConfig"
set "OUT_DIR=%SRC_DIR%\temp\cfg"
set "BUILD_DIR=%SRC_DIR%\build"

if not exist "%GUI_SRC%\main.cpp" (
    echo   [ERROR] 未找到 GUI 源码: %GUI_SRC%\main.cpp
    exit /b 1
)

call :FindVCVars
if not defined VCVARS (
    echo   [ERROR] 未找到 vcvarsall.bat, 请安装 Visual Studio 2019+ 的 C++ 桌面开发工作负载
    exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

call "%VCVARS%" x64 >nul
if errorlevel 1 exit /b 1

echo   [INFO] Compiling resource (.rc -^> .res)...
rem   资源脚本里的 manifest.xml / icon.ico 都按 rc 文件所在目录解析, 所以先切过去
pushd "%GUI_SRC%"
rc.exe /nologo /fo "%OUT_DIR%\BorderLimitedConfig.res" BorderLimitedConfig.rc
if errorlevel 1 (
    popd
    echo   [ERROR] 资源编译失败
    exit /b 1
)
popd

echo   [INFO] Compiling BorderLimitedConfig.exe...
cl.exe /nologo /utf-8 /O2 /MT /EHsc /D WIN32_LEAN_AND_MEAN /D NDEBUG /Fo"%OUT_DIR%\\" /Fe"%BUILD_DIR%\BorderLimitedConfig.exe" "%GUI_SRC%\main.cpp" "%GUI_SRC%\config_io.cpp" /link user32.lib kernel32.lib comctl32.lib gdi32.lib "%OUT_DIR%\BorderLimitedConfig.res"
if errorlevel 1 exit /b 1

echo.
echo   [INFO] Done:
echo     %BUILD_DIR%\BorderLimitedConfig.exe
exit /b 0

rem   ===================================================================
rem   :FindVCVars - 优先用 vswhere 定位, 失败时扫描常见安装位置, 结果写入 VCVARS
rem   ===================================================================
:FindVCVars
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "delims=" %%I in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do (
        if exist "%%I\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%I\VC\Auxiliary\Build\vcvarsall.bat"
    )
)
if defined VCVARS exit /b 0

for %%D in (
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
    "%ProgramFiles%\Microsoft Visual Studio\2019\Community"
    "%ProgramFiles%\Microsoft Visual Studio\2019\Professional"
    "%ProgramFiles%\Microsoft Visual Studio\2019\Enterprise"
    "%ProgramFiles%\Microsoft Visual Studio\2019\BuildTools"
) do if exist "%%~D\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%~D\VC\Auxiliary\Build\vcvarsall.bat"
exit /b 0
