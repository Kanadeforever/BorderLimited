chcp 65001 >nul
cls
@echo on
setlocal
rem  ===================================================================  
rem  build_asi.bat - BorderLimited ASI 插件双架构编译脚本 (x64 + x86)  
rem
rem  编译输出:  
rem  source\build\BorderLimited.x64.asi   (64 位游戏)  
rem  source\build\BorderLimited.x86.asi   (32 位游戏)  
rem
rem  依赖:  
rem  - Visual Studio 2019+ (MSVC) + Windows SDK  
rem  - MinHook (BSD 2-Clause) 随仓库提供: thirdparty\minhook  
rem
rem  用法:  
rem  在 source 目录下双击运行, 或命令行执行 source\build_asi.bat  
rem
rem  说明:  
rem  所有路径都从本脚本自身位置推导, 仓库整体移动/改名后无需修改本文件  
rem  两个架构分别在独立的 cmd 进程里编译, 因为 vcvarsall 在同一会话里  
rem  连续调用两次会报 "The input line is too long."  
rem  ===================================================================  

rem  ---- 目录推导 ----  
rem  SRC_DIR  = <repo>\source       (%~dp0 就是 source 目录)  
rem  ROOT_DIR = <repo>  
for %%I in ("%~dp0.") do set "SRC_DIR=%%~fI"
for %%I in ("%~dp0..") do set "ROOT_DIR=%%~fI"

set "PLUGIN_SRC=%SRC_DIR%\src\BorderLimited"
set "MINHOOK=%ROOT_DIR%\thirdparty\minhook"
set "OUT_DIR=%SRC_DIR%\temp\asi"
set "BUILD_DIR=%SRC_DIR%\build"

if not exist "%PLUGIN_SRC%\main.cpp" (
    echo  [ERROR] 未找到插件源码: %PLUGIN_SRC%\main.cpp
    exit /b 1
)
if not exist "%MINHOOK%\include\MinHook.h" (
    echo  [ERROR] 未找到 MinHook: %MINHOOK%\include\MinHook.h
    exit /b 1
)

call :FindVCVars
if not defined VCVARS (
    echo  [ERROR] 未找到 vcvarsall.bat, 请安装 Visual Studio 2019+ 的 C++ 桌面开发工作负载
    exit /b 1
)

if not exist "%OUT_DIR%\x64" mkdir "%OUT_DIR%\x64"
if not exist "%OUT_DIR%\x86" mkdir "%OUT_DIR%\x86"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

rem  ---- 架构派发 ----
rem  用专用开关 --arch=x64 / --arch=x86 进入单架构流程，
rem  不再依赖 %1，避免调用方式把参数污染成 x64/x86 后静默只编一个架构。
set "ARCH="
for %%A in (%*) do (
    if /i "%%~A"=="--arch=x64" set "ARCH=x64"
    if /i "%%~A"=="--arch=x86" set "ARCH=x86"
)
if /i "%ARCH%"=="x64" goto BuildX64
if /i "%ARCH%"=="x86" goto BuildX86

echo  === x64 ===
cmd /c ""%~f0" --arch=x64"
if errorlevel 1 (
    echo  [ERROR] x64 编译失败
    exit /b 1
)
if not exist "%BUILD_DIR%\BorderLimited.x64.asi" (
    echo  [ERROR] x64 产物缺失: %BUILD_DIR%\BorderLimited.x64.asi
    exit /b 1
)

echo  === x86 ===
cmd /c ""%~f0" --arch=x86"
if errorlevel 1 (
    echo  [ERROR] x86 编译失败
    exit /b 1
)
if not exist "%BUILD_DIR%\BorderLimited.x86.asi" (
    echo  [ERROR] x86 产物缺失: %BUILD_DIR%\BorderLimited.x86.asi
    exit /b 1
)

echo.
echo  [INFO] Done:
echo  %BUILD_DIR%\BorderLimited.x64.asi
echo  %BUILD_DIR%\BorderLimited.x86.asi
exit /b 0

rem  ===================================================================  
rem  单架构编译流程 (由上面的 cmd /c 以参数方式进入)  
rem  ===================================================================  
:BuildX64
call "%VCVARS%" x64 >nul
if errorlevel 1 exit /b 1
rem  MinHook 头文件搜索路径 (vcvarsall 会重置 INCLUDE, 所以每次都要重新追加)  
set "INCLUDE=%MINHOOK%\include;%MINHOOK%\src;%INCLUDE%"
cl.exe /nologo /utf-8 /O2 /MT /LD /EHsc /D WIN32_LEAN_AND_MEAN /D NDEBUG /Fo"%OUT_DIR%\x64\\" /Fe"%OUT_DIR%\BorderLimited.x64.dll" "%PLUGIN_SRC%\main.cpp" "%PLUGIN_SRC%\config.cpp" "%PLUGIN_SRC%\window.cpp" "%PLUGIN_SRC%\ue3.cpp" "%MINHOOK%\src\buffer.c" "%MINHOOK%\src\hook.c" "%MINHOOK%\src\trampoline.c" "%MINHOOK%\src\hde\hde32.c" "%MINHOOK%\src\hde\hde64.c" /link user32.lib kernel32.lib
if errorlevel 1 exit /b 1
copy /y "%OUT_DIR%\BorderLimited.x64.dll" "%BUILD_DIR%\BorderLimited.x64.asi" >nul
if errorlevel 1 exit /b 1
echo  [OK] x64 built
exit /b 0

:BuildX86
call "%VCVARS%" x86 >nul
if errorlevel 1 exit /b 1
set "INCLUDE=%MINHOOK%\include;%MINHOOK%\src;%INCLUDE%"
cl.exe /nologo /utf-8 /O2 /MT /LD /EHsc /D WIN32_LEAN_AND_MEAN /D NDEBUG /Fo"%OUT_DIR%\x86\\" /Fe"%OUT_DIR%\BorderLimited.x86.dll" "%PLUGIN_SRC%\main.cpp" "%PLUGIN_SRC%\config.cpp" "%PLUGIN_SRC%\window.cpp" "%PLUGIN_SRC%\ue3.cpp" "%MINHOOK%\src\buffer.c" "%MINHOOK%\src\hook.c" "%MINHOOK%\src\trampoline.c" "%MINHOOK%\src\hde\hde32.c" "%MINHOOK%\src\hde\hde64.c" /link user32.lib kernel32.lib
if errorlevel 1 exit /b 1
copy /y "%OUT_DIR%\BorderLimited.x86.dll" "%BUILD_DIR%\BorderLimited.x86.asi" >nul
if errorlevel 1 exit /b 1
echo  [OK] x86 built
exit /b 0

rem  ===================================================================  
rem  :FindVCVars - 优先用 vswhere 定位, 失败时扫描常见安装位置, 结果写入 VCVARS  
rem  ===================================================================  
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
