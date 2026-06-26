#pragma once

// ===================================================================
// window.h — 窗口操作 API 声明
//
// WindowManager 命名空间提供 BorderLimited ASI 插件的所有窗口级操作。
// 对标原版 Borderless Gaming 的 Manipulation.cs（窗口修改）和
// Native.cs（系统级功能）。
//
// 【线程安全性约定】
//   本文件中声明的函数不提供内部同步机制。
//   调用方（main.cpp/ue3.cpp）负责在线程安全上下文中调用。
//
//   各函数的典型调用线程：
//     ApplyBorderless        — WorkerThread / UE3 D3D9 回调线程
//     SaveOriginalState      — WorkerThread / UE3 D3D9 回调线程
//     RestoreWindow          — HotkeyThread
//     LockCursorToWindow     — WorkerThread / HotkeyThread
//     HideWindowsTaskbar     — WorkerThread / UE3 首次设备回调
//     HideMouseCursor        — WorkerThread / UE3 首次设备回调
//     StartWinKeyHook        — DllMain 线程（仅一次）
//     OverrideDpiScaling     — DllMain 线程（仅一次）
//
//   内部 static 变量的同步由各自的实现保证（atomic / CAS / release-acquire）
// ===================================================================

#include "config.h"

namespace WindowManager {

    // ================================================================
    // 无边框核心操作
    // ================================================================

    // 将目标窗口转换为无边框全屏
    // 9 步执行流程：验证→菜单→样式→矩形→偏移→SetWindowPos→最大化→置顶→EXITSIZEMOVE
    // 返回: true=操作成功, false=窗口无效
    bool ApplyBorderless(const AppConfig& config, HWND hwnd);

    // 保存窗口原始状态（在 ApplyBorderless 前调用）
    // 每次调用覆盖上次保存的值：允许跟踪窗口重建后的新状态
    void SaveOriginalState(HWND hwnd);

    // 检查已保存状态是否有效（rect 有非零面积）
    bool IsValidSavedState();

    // 恢复到保存的原始窗口状态
    // 双检 acquire 保护 TOCTOU：拷贝前后各 check 一次 g_savedStateReady
    // 返回 true=恢复成功
    bool RestoreWindow(HWND hwnd);

    // ================================================================
    // 辅助窗口功能
    // ================================================================

    // ClipCursor 锁定鼠标在窗口边界内
    void LockCursorToWindow(HWND hwnd);
    // ClipCursor(NULL) 解除锁定
    void UnlockCursor();

    // ================================================================
    // 系统功能（有状态，需要配对调用）
    // ================================================================

    // WH_KEYBOARD_LL 钩子禁用 Win 键（独立线程+消息泵）
    void StartWinKeyHook();
    void StopWinKeyHook();  // WM_QUIT + WaitForSingleObject

    // 隐藏/恢复任务栏 + 扩展/恢复桌面工作区
    void HideWindowsTaskbar(bool hide);

    // 替换系统箭头光标为 1x1 透明光标 / 恢复原始箭头光标
    // CAS 原子保证多线程安全（一次只有一个线程执行切换）
    void HideMouseCursor(bool hide);

    // 三层回退禁用 DPI 虚拟化：PerMonitorV2 → PerMonitor → System
    // 必须在任何窗口创建前调用（DllMain 中尽早执行）
    void OverrideDpiScaling();

} // namespace WindowManager
