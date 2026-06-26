#!/bin/bash
# ===================================================================
# build.sh — BorderLimited ASI 插件双架构编译脚本
#
# 编译输出:
#   workspace/build/BorderLimited.x64.asi  (x64 原生 DLL)
#   workspace/build/BorderLimited.x86.asi  (x86 原生 DLL)
#
# 依赖:
#   - Visual Studio 2019+ (MSVC) + Windows SDK
#   - MinHook (BSD 2-Clause) — 源码在 参考项目/minhook-master/
#
# 运行方法:
#   bash workspace/scripts/build.sh
#
# 注意: 构建脚本将 MinHook 复制到临时目录以避免中文路径编码问题
# ===================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE="$(cd "$SCRIPT_DIR/.." && pwd)"
MH_SRC="$WORKSPACE/../参考项目/minhook-master"
MH_TEMP="$WORKSPACE/temp/minhook-build"
BUILD_DIR="$WORKSPACE/build"

echo "[INFO] Preparing MinHook sources..."
rm -rf "$MH_TEMP"
cp -r "$MH_SRC" "$MH_TEMP"

echo "[INFO] Running MSVC build..."
# 生成 batch 文件
cat > "$WORKSPACE/temp/build_both.bat" << 'BATEOF'
@echo off
setlocal enabledelayedexpansion
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 > nul
if errorlevel 1 exit /b 1

set SRC=C:\Project\Software\borderlessgaming-asi\workspace\src\BorderLimited
set MH=C:\Project\Software\borderlessgaming-asi\workspace\temp\minhook-build
set OUT=C:\Project\Software\borderlessgaming-asi\workspace\temp
set BUILD=C:\Project\Software\borderlessgaming-asi\workspace\build

if not exist "%OUT%\x64" mkdir "%OUT%\x64"
if not exist "%OUT%\x86" mkdir "%OUT%\x86"
if not exist "%BUILD%" mkdir "%BUILD%"

echo === x64 ===
cl.exe /nologo /utf-8 /O2 /MT /LD /EHsc /D WIN32_LEAN_AND_MEAN /D NDEBUG /I "%MH%\include" /I "%MH%\src" /Fo"%OUT%\x64\\" /Fe"%OUT%\BorderLimited.x64.dll" "%SRC%\main.cpp" "%SRC%\config.cpp" "%SRC%\window.cpp" "%SRC%\ue3.cpp" "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde32.c" "%MH%\src\hde\hde64.c" /link user32.lib kernel32.lib
if errorlevel 1 exit /b 1
copy /y "%OUT%\BorderLimited.x64.dll" "%BUILD%\BorderLimited.x64.asi"
echo [OK] x64 built

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 > nul
if errorlevel 1 exit /b 1

echo === x86 ===
cl.exe /nologo /utf-8 /O2 /MT /LD /EHsc /D WIN32_LEAN_AND_MEAN /D NDEBUG /I "%MH%\include" /I "%MH%\src" /Fo"%OUT%\x86\\" /Fe"%OUT%\BorderLimited.x86.dll" "%SRC%\main.cpp" "%SRC%\config.cpp" "%SRC%\window.cpp" "%SRC%\ue3.cpp" "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde32.c" "%MH%\src\hde\hde64.c" /link user32.lib kernel32.lib
if errorlevel 1 exit /b 1
copy /y "%OUT%\BorderLimited.x86.dll" "%BUILD%\BorderLimited.x86.asi"
echo [OK] x86 built

echo Build complete.
BATEOF

# 使用 Windows 原生 cmd.exe 执行批处理，避免 bash 路径编码问题
cmd.exe //c "$WORKSPACE\\temp\\build_both.bat" 2>&1

echo "[INFO] Cleanup..."
rm -rf "$MH_TEMP"

echo "[INFO] Done: $BUILD_DIR/BorderLimited.x64.asi, $BUILD_DIR/BorderLimited.x86.asi"
