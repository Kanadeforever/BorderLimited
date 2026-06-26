#!/bin/bash
# ===================================================================
# build_gui.sh — BorderLimited Config GUI 编译脚本
#
# 编译输出: workspace/build/BorderLimitedConfig.exe (单文件, 无依赖)
#
# 依赖与 build.sh 相同: MSVC 2019 + Windows SDK 10.0.26100.0
#
# 编译步骤:
#   1. rc.exe 编译资源文件 (.rc → .res) — 包含对话框布局和图标
#   2. cl.exe 编译 C++ 源码 + 链接资源 → .exe
#
# 运行方法:
#   bash workspace/scripts/build_gui.sh
# ===================================================================
set -e
export MSYS_NO_PATHCONV=1

W="c:/Project/Software/borderlessgaming-asi/workspace"
SRC="$W/src/BorderLimitedConfig"
M="c:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.29.30133"
S="c:/Program Files (x86)/Windows Kits/10"
V="10.0.26100.0"

# 资源编译器路径
RC="c:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe"

mkdir -p "$W/temp/gui" "$W/build"

# ---- 1. 编译资源文件 ----
echo "[INFO] Compiling resource (.rc → .res)..."
"$RC" /nologo \
    /i "$S/Include/$V/um" \
    /i "$S/Include/$V/shared" \
    /fo "$W/temp/gui/BorderLimitedConfig.res" \
    "$SRC/BorderLimitedConfig.rc"

# ---- 2. 编译链接 exe ----
echo "[INFO] Compiling BorderLimitedConfig.exe..."
"$M/bin/HostX64/x64/cl.exe" /nologo /utf-8 /O2 /MT /EHsc \
    /D WIN32_LEAN_AND_MEAN /D NDEBUG \
    /I "$M/include" /I "$M/atlmfc/include" \
    /I "$S/Include/$V/ucrt" /I "$S/Include/$V/um" /I "$S/Include/$V/shared" \
    "/Fo$W/temp/gui/" "/Fe$W/build/BorderLimitedConfig.exe" \
    "$SRC/main.cpp" "$SRC/config_io.cpp" \
    /link "/LIBPATH:$M/lib/x64" "/LIBPATH:$S/Lib/$V/um/x64" "/LIBPATH:$S/Lib/$V/ucrt/x64" \
    user32.lib kernel32.lib comctl32.lib gdi32.lib "$W/temp/gui/BorderLimitedConfig.res"

echo "[INFO] Done: $W/build/BorderLimitedConfig.exe"
