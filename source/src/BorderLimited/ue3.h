#pragma once

// ===================================================================
// ue3.h — 虚幻引擎 3 D3D9 Hook 模式
//
// 【为什么需要特殊的 Hook 模式？】
//   普通轮询模式每 PollingIntervalMs 检查一次窗口样式。
//   UE3 引擎在每帧（16ms）都会调用 SetWindowLong 恢复窗口样式，
//   并使用独占全屏 (Windowed=FALSE) 创建 D3D9 设备。
//   轮询间隔追不上每帧频率，必须用 API Hook 在调用发生时实时拦截。
//
// 【Hook 分层架构】
//   窗口层:
//     SetWindowLongA/W → 篡改样式位（使用与轮询路径相同的掩码常量）
//     SetWindowPos     → 强制全屏几何（仅拦截游戏主窗口）
//   D3D9 层:
//     Direct3DCreate9  → VTable 补丁 IDirect3D9 的两个虚函数:
//       CreateDevice          [索引16] → 强制 Windowed=TRUE
//       GetAdapterDisplayMode [索引8]  → 返回伪造分辨率
//   热键层:
//     g_bHooksActive 原子开关 → 所有 Hook 直通/拦截切换
//
// 【与轮询模式的关系】
//   UE3Mode=1 + d3d9.dll 可用 → 纯 Hook 路径（无 WorkerThread）
//   UE3Mode=1 + d3d9.dll 不可用 → 自动回退到轮询路径
//   两种模式都运行 HotkeyThread（热键始终可用）
//
// 【设计参考】
//   GeDoSaTo by Durante — "篡改 API 参数欺骗引擎"的核心思路
//   MinHook  by Tsuda Kageyu — 轻量级 x86/x64 API Hook 库
//
// 【使用】
//   INI 中设置 UE3Mode=1 → ASI 加载后自动激活
// ===================================================================

#include "config.h"

namespace UE3 {

    // 初始化所有 Hook（先 d3d9 后 user32，失败时 MH_Uninitialize 清理残留）
    // cfg: 配置快照（Hook 回调中通过副本读取，避免跨编译单元依赖）
    // 返回: true=全部 Hook 就绪, false=回退到轮询模式
    bool Init(const AppConfig& cfg);

    // 卸载所有 Hook 并释放 MinHook 资源（DLL_PROCESS_DETACH 时调用）
    void Shutdown();

    // 热键切换：启用/禁用 Hook 拦截
    // atomic store（HotkeyThread 写，D3D9 回调线程读）
    void SetActive(bool active);
    bool IsActive();

} // namespace UE3
