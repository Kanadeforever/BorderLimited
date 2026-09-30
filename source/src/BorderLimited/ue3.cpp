// ===================================================================
// ue3.cpp — UE3 D3D9 Hook 实现
//
// 【本文件的职责】
//   为虚幻引擎 3 游戏提供 API 级别的窗口/渲染拦截。UE3 引擎的行为
//   与普通游戏不同，轮询模式无法处理，必须用 Hook 实时篡改 API 调用。
//
// 【为什么 UE3 游戏需要特殊处理？】
//   普通游戏：创建窗口 → 设置样式 → 不再主动修改 → 轮询一次就够
//   UE3 游戏：创建窗口 → 设置样式 → 每帧调用 SetWindowLong 恢复样式
//            → 调用 Direct3DCreate9 → CreateDevice(Windowed=FALSE)
//   轮询间隔（默认 1000ms）追不上每帧调用的频率，必须用 API Hook
//   在调用发生时就拦截并篡改参数。
//
// 【Hook 架构 — 5 个拦截点】
//
//   层级 1: 窗口样式（阻止游戏恢复边框）
//     HkSetWindowLongA/W  → 篡改 GWL_STYLE / GWL_EXSTYLE 位掩码
//     HkSetWindowPos       → 篡改窗口位置和大小
//
//   层级 2: D3D9 设备（阻止独占全屏）
//     HkDirect3DCreate9    → 对返回的 IDirect3D9* 进行 VTable 补丁
//       └─ HkCreateDevice         → 强制 Windowed=TRUE
//       └─ HkGetAdapterDisplayMode → 返回伪造分辨率
//
// 【设计参考】
//   核心思路来自 GeDoSaTo (by Durante) — 在 API 层欺骗游戏引擎
//   MinHook 来自 Tsuda Kageyu (BSD 2-Clause)
// ===================================================================

#include "ue3.h"
#include "window.h"
#include "native.h"  // WindowStyle::StylesToRemove / ExtendedStylesToRemove — 统一样式掩码

#include <atomic>  // std::atomic — 跨线程无锁通信

// g_hLastWindow 在 main.cpp 中定义为 std::atomic<HWND>
// 这里用 extern 跨编译单元引用，使 HkCreateDevice 能设置窗口句柄
// 为什么不是 static？static 限制为文件作用域，其他 .cpp 无法 extern
extern std::atomic<HWND> g_hLastWindow;

// MinHook — 轻量级 x86/x64 API Hook 库，通过改写目标函数入口的
// 前几条指令实现跳转（JMP）到我们的钩子函数，同时生成跳板函数
// 以便在钩子内部调用原始 API
#include "..\..\..\参考项目\minhook-master\include\MinHook.h"

#include <d3d9.h>   // IDirect3D9, D3DPRESENT_PARAMETERS, D3DDISPLAYMODE

// ===================================================================
// 跳板函数指针 — Hook 机制的核心基础设施
//
// 【什么是跳板（Trampoline）？】
//   MinHook 在安装钩子时：
//     1. 保存目标函数的前 N 条指令到一块新内存（跳板）
//     2. 在目标函数入口写入 JMP 到我们的钩子函数
//     3. 跳板 = 原始入口指令 + JMP 回目标函数剩余部分
//   这样钩子函数可以调用跳板来执行"真正的原始逻辑"。
//
//   HkSetWindowLongA → 篡改参数 → TrueSetWindowLongA（原始函数）
//                                     ↑ 这就是跳板
//
// 【函数指针类型定义】
//   每个 Hook 目标都需要精确的函数签名（参数类型、调用约定、返回值），
//   否则栈帧错位导致崩溃。WINAPI = __stdcall，x64 下调用约定统一。
// ===================================================================
typedef LONG  (WINAPI *SetWindowLongA_t)(HWND, int, LONG);
typedef LONG  (WINAPI *SetWindowLongW_t)(HWND, int, LONG);
typedef BOOL  (WINAPI *SetWindowPos_t  )(HWND, HWND, int, int, int, int, UINT);
typedef IDirect3D9* (WINAPI *Direct3DCreate9_t)(UINT);

// D3D9 VTable 方法签名
// COM 接口的方法第一个参数是 this 指针（IDirect3D9*），
// 这是 COM 的调用约定 — this 通过第一个参数显式传入而非寄存器/隐式
typedef HRESULT (WINAPI *CreateDevice_t)         (IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (WINAPI *GetAdapterDisplayMode_t)(IDirect3D9*, UINT, D3DDISPLAYMODE*);

// ---- 跳板指针（MinHook 在 MH_CreateHook 时填充） ----
static SetWindowLongA_t        TrueSetWindowLongA        = nullptr;
static SetWindowLongW_t        TrueSetWindowLongW        = nullptr;
static SetWindowPos_t          TrueSetWindowPos          = nullptr;
static Direct3DCreate9_t       TrueDirect3DCreate9       = nullptr;
static CreateDevice_t          TrueCreateDevice          = nullptr;
static GetAdapterDisplayMode_t TrueGetAdapterDisplayMode = nullptr;

// ===================================================================
// 全局 UE3 配置快照
//
// 【为什么需要快照而不是直接读 g_Config？】
//   g_Config（在 main.cpp 中）是"当前运行时配置"。但 UE3 的 Hook 回调
//   可能在 D3D9 工作线程中执行，访问 g_Config 需要跨编译单元 extern。
//   快照在 Init 时复制一份，之后各 Hook 只读本地副本，避免跨文件依赖。
// ===================================================================
static int       g_UE3RenderWidth  = 0;
static int       g_UE3RenderHeight = 0;
static AppConfig g_UE3Config;       // Init 时从 cfg 复制的完整配置

// ===================================================================
// g_bHooksActive — 热键控制的 Hook 开关
//
// 【为什么用 atomic 而不是普通 bool？】
//   写操作：HotkeyThread 通过 UE3::SetActive() 写入
//   读操作：所有 Hook 函数（在 D3D9 回调线程 / 游戏主线程中执行）
//   如果不用 atomic，编译器可能把值缓存在寄存器中，HotkeyThread 的
//   写入永远不会被 Hook 函数看到。std::atomic 强制内存可见性。
//
// 【relaxed 为什么足够？】
//   不需要顺序保证，只需要"值最终会被看到"。relaxed 在 x86/x64 上
//   编译为普通 MOV 指令，零性能开销。
// ===================================================================
static std::atomic<bool> g_bHooksActive{true};

// ===================================================================
// AdjustStyleBits — 位掩码层面清除窗口边框样式
//
// 【为什么是 Hook 的核心？】
//   UE3 引擎每次调用 SetWindowLong 都会传入包含 WS_CAPTION 等样式的值。
//   如果我们只是"在调用后再次 SetWindowLong 恢复无边框"，引擎下一帧又会覆盖。
//   正确做法：在参数传给真正 API 之前就篡改，引擎永远看不到带边框的样式。
//
// 【XOR 位清除 — 为什么不用 &= ~mask？】
//   (value | mask) ^ mask 与 value & ~mask 位运算结果相同。
//   但 XOR 版本对符号扩展免疫。LONG 是有符号类型，~mask 在某些编译器
//   警告级别下可能触发符号翻转警告。XOR 版本两边都是正整数运算，无歧义。
//   此技巧参考 SRWE (Simple Runtime Window Editor)。
//
// 【为什么用 native.h 中的 WindowStyle 常量而不是局部定义？】
//   轮询路径的 ApplyBorderless 使用同一组常量。如果两边不一致，
//   热键切换时"恢复的样式"和"清除的样式"可能不匹配，导致窗口状态异常。
// ===================================================================
static void AdjustStyleBits(int nIndex, LONG& dwNewLong) {
    if (nIndex == GWL_STYLE) {
        dwNewLong = (dwNewLong | WindowStyle::StylesToRemove) ^ WindowStyle::StylesToRemove;
    }
    else if (nIndex == GWL_EXSTYLE) {
        dwNewLong = (dwNewLong | WindowStyle::ExtendedStylesToRemove) ^ WindowStyle::ExtendedStylesToRemove;
    }
    // 注意：nIndex 可能是其他值（如 GWL_ID, GWL_USERDATA），此时不做修改
}

// ===================================================================
// HkSetWindowLongA / HkSetWindowLongW — SetWindowLong 钩子
//
// 【为什么两个版本都需要 Hook？】
//   ANSI 版 (SetWindowLongA) 和 Unicode 版 (SetWindowLongW) 是两个
//   不同的 API 入口。游戏可能调用任意一个，取决于它是 ANSI 还是 Unicode
//   编译。同时 Hook 两个确保不漏。
//
//   MSVC 默认 Unicode → 游戏大概率调 SetWindowLongW，
//   但也不排除某些老游戏或第三方库用 ANSI 版本。
//
// 【g_bHooksActive 检查的作用】
//   当用户按 Alt+F10 关闭 Hook 时，所有 Hook 函数执行原始 API 不做篡改。
//   这样游戏恢复原生窗口行为，用户可以拖动窗口、操作菜单栏等。
//   再次按热键 → g_bHooksActive=true → Hook 重新拦截。
// ===================================================================
static LONG WINAPI HkSetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong) {
    // 热键关闭了 Hook → 直通原始 API，不做任何修改
    if (!g_bHooksActive.load(std::memory_order_relaxed))
        return TrueSetWindowLongA(hWnd, nIndex, dwNewLong);

    // 拷贝 → 篡改 → 传递（不改原值，保持调用方数据不变）
    LONG adjusted = dwNewLong;
    AdjustStyleBits(nIndex, adjusted);
    return TrueSetWindowLongA(hWnd, nIndex, adjusted);
}

static LONG WINAPI HkSetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) {
    if (!g_bHooksActive.load(std::memory_order_relaxed))
        return TrueSetWindowLongW(hWnd, nIndex, dwNewLong);

    LONG adjusted = dwNewLong;
    AdjustStyleBits(nIndex, adjusted);
    return TrueSetWindowLongW(hWnd, nIndex, adjusted);
}

// ===================================================================
// HkSetWindowPos — 拦截窗口位置/大小改变
//
// 【为什么需要过滤窗口？】
//   之前版本对所有 SetWindowPos 调用生效。但游戏不仅仅是主窗口调用它 —
//   对话框、弹出菜单、子控件等也调用 SetWindowPos。
//   不加过滤会导致这些窗口被强制拉成全屏，布局完全崩坏。
//   现在通过与 g_hLastWindow 比较，只对游戏主窗口生效。
//
// 【纯 Z 序操作为什么放行？】
//   (SWP_NOMOVE | SWP_NOSIZE) 表示"只改变窗口在 Z 轴上的顺序，
//   不改变位置和大小"。例如 BringWindowToTop 内部就调用这个。
//   这种操作不需要拦截，放行即可。
//
// 【SWP_FRAMECHANGED 标志的作用】
//   通知 Windows："窗口框架样式已改变，请重新计算非客户区"。
//   不加这个标志的话，即使我们改了 GWL_STYLE，窗口外观不会立即刷新。
//   对标 MSDN: "如果改变了窗口样式，应设置此标志使更改生效"。
// ===================================================================
static BOOL WINAPI HkSetWindowPos(
    HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    if (!g_bHooksActive.load(std::memory_order_relaxed))
        return TrueSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);

    // 纯 Z 序操作（不涉及位置和大小）→ 原样放行
    if ((uFlags & SWP_NOMOVE) && (uFlags & SWP_NOSIZE)) {
        return TrueSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
    }

    // 窗口过滤：只拦截游戏主窗口，子窗口/对话框等不受影响
    HWND hLast = g_hLastWindow.load(std::memory_order_relaxed);
    if (!hLast || hWnd != hLast) {
        return TrueSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
    }

    // 强制将目标矩形替换为显示器完整分辨率
    // MonitorFromWindow: 根据窗口当前位置选择它所在的显示器
    // MONITOR_DEFAULTTONEAREST: 如果窗口不在任何显示器上，选最近的
    HMONITOR hMon = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };  // cbSize 必须在调用前填充！
    if (::GetMonitorInfoW(hMon, &mi)) {
        X  = mi.rcMonitor.left;
        Y  = mi.rcMonitor.top;
        cx = mi.rcMonitor.right  - mi.rcMonitor.left;
        cy = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    // 去掉 NOMOVE/NOSIZE（因为我们要改变位置/大小）
    // 加上 FRAMECHANGED（通知窗口管理器重算非客户区）
    return TrueSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy,
        (uFlags & ~(SWP_NOMOVE | SWP_NOSIZE)) | SWP_FRAMECHANGED);
}

// ===================================================================
// HkCreateDevice — 拦截 IDirect3D9::CreateDevice
//
// 【这是 UE3 模式最关键的 Hook】
//   UE3 引擎创建 D3D9 设备时传入 Windowed=FALSE（独占全屏模式）。
//   独占全屏模式的特点：
//     - 窗口覆盖整个屏幕，Alt+Tab 有模式切换延迟
//     - 多显示器时其他屏幕可能变黑或分辨率变化
//     - 窗口样式修改可能被 D3D9 运行时覆盖
//   我们要的是"无边框窗口全屏"：仍然是窗口模式（Windowed=TRUE），
//   但窗口覆盖整个显示器、没有边框和标题栏。
//
// 【pParams 篡改与还原】
//   CreateDevice 可能因为 GPU 不支持请求的格式等原因失败。
//   如果失败，调用方可能会检查 pParams 来做错误处理（例如降低分辨率重试）。
//   如果我们不还原 pParams，调用方看到 Windowed=TRUE 会产生错误判断。
//   保存原始值 → 成功就用不改回（调用方不关心），失败就还原。
//
// 【s_firstDeviceDone — 一次性初始化】
//   CAS 原子保护：即使多个 D3D9 线程同时首次到达，只有一个执行初始化。
//   初始化内容：保存原始窗口状态、隐藏任务栏、隐藏光标（幂等操作）。
//
// 【ApplyBorderless 每次设备创建都执行】
//   不是一次性操作：如果游戏重建设备（切分辨率等），需要重新应用。
//   但 SaveOriginalState 只执行一次（保存的是游戏最初的状态）。
// ===================================================================
static HRESULT WINAPI HkCreateDevice(
    IDirect3D9* pThis, UINT Adapter, D3DDEVTYPE DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pParams, IDirect3DDevice9** ppReturnedDeviceInterface)
{
    if (!g_bHooksActive.load(std::memory_order_relaxed))
        return TrueCreateDevice(pThis, Adapter, DeviceType, hFocusWindow,
                                BehaviorFlags, pParams, ppReturnedDeviceInterface);

    // ---- 保存 pParams 原始值（用于失败还原） ----
    BOOL origWindowed    = (pParams ? pParams->Windowed : FALSE);
    UINT origRefreshRate = (pParams ? pParams->FullScreen_RefreshRateInHz : 0);

    // ---- 强制窗口模式 ----
    // FullScreen_RefreshRateInHz 在 Windowed=TRUE 时必须为 0（D3D9 文档要求）
    if (pParams && !pParams->Windowed) {
        pParams->Windowed = TRUE;
        pParams->FullScreen_RefreshRateInHz = 0;
    }

    // ---- 调用原始 CreateDevice ----
    HRESULT hr = TrueCreateDevice(pThis, Adapter, DeviceType,
        hFocusWindow, BehaviorFlags, pParams, ppReturnedDeviceInterface);

    // ---- 失败时还原 pParams ----
    if (FAILED(hr) && pParams) {
        pParams->Windowed = origWindowed;
        pParams->FullScreen_RefreshRateInHz = origRefreshRate;
    }

    // ---- 成功后应用无边框 ----
    if (SUCCEEDED(hr) && hFocusWindow && ::IsWindow(hFocusWindow)) {
        // 一次性初始化（CAS 保证只执行一次）
        static std::atomic<bool> s_firstDeviceDone{false};
        bool expected = false;
        if (s_firstDeviceDone.compare_exchange_strong(expected, true)) {
            WindowManager::SaveOriginalState(hFocusWindow);

            if (g_UE3Config.HideWindowsTaskbar) {
                WindowManager::HideWindowsTaskbar(true);
            }

            if (g_UE3Config.HideMouseCursor) {
                WindowManager::HideMouseCursor(true);
            }
        }

        // 每次设备创建都重新应用（游戏可能重建设备）
        WindowManager::ApplyBorderless(g_UE3Config, hFocusWindow);

        // 每次重建设备都更新光标裁剪区域
        if (g_UE3Config.LockCursor) {
            WindowManager::LockCursorToWindow(hFocusWindow);
        }

        // 记录窗口句柄 — HotkeyThread 和 HkSetWindowPos 需要
        g_hLastWindow.store(hFocusWindow, std::memory_order_relaxed);
    }

    return hr;
}

// ===================================================================
// HkGetAdapterDisplayMode — 向游戏返回伪造的显示模式
//
// 【目的】
//   游戏用 GetAdapterDisplayMode 查询"当前桌面的分辨率和刷新率"，
//   来判断自己是否成功进入全屏模式。如果我们强制了 Windowed=TRUE 但
//   不伪造这个返回值，游戏会发现"分辨率不匹配"，可能回退到独占全屏。
//
// 【只在 INI 中明确设置时生效】
//   UE3RenderWidth=0 时不伪造：大部分游戏不需要，伪造反而可能导致
//   渲染分辨率与实际显示器不匹配。
//   只有那些明确检查分辨率的游戏才需要设置。
// ===================================================================
static HRESULT WINAPI HkGetAdapterDisplayMode(
    IDirect3D9* pThis, UINT Adapter, D3DDISPLAYMODE* pMode)
{
    if (!g_bHooksActive.load(std::memory_order_relaxed))
        return TrueGetAdapterDisplayMode(pThis, Adapter, pMode);

    HRESULT hr = TrueGetAdapterDisplayMode(pThis, Adapter, pMode);
    if (SUCCEEDED(hr) && g_UE3RenderWidth > 0 && g_UE3RenderHeight > 0) {
        pMode->Width  = (UINT)g_UE3RenderWidth;
        pMode->Height = (UINT)g_UE3RenderHeight;
        // 不修改 RefreshRate, Format 等字段 — 只改分辨率
    }
    return hr;
}

// ===================================================================
// HkDirect3DCreate9 — 拦截 D3D9 工厂创建 + VTable 补丁
//
// 【COM 虚函数表（VTable）补丁原理】
//   IDirect3D9 是一个 COM 接口。COM 对象的前 4/8 字节是指向虚函数表
//   (VTable) 的指针。VTable 是一个函数指针数组：
//
//     IDirect3D9* pD3D9;
//     void** vtable = *(void***)pD3D9;  // 解引用获取 VTable 指针
//     vtable[0]  = QueryInterface
//     vtable[1]  = AddRef
//     vtable[2]  = Release
//     ...
//     vtable[8]  = GetAdapterDisplayMode  ← 我们挂钩的
//     ...
//     vtable[16] = CreateDevice           ← 我们挂钩的
//
//   MinHook 修改的是目标函数入口的代码（写入 JMP），不是 VTable 中的
//   指针本身。所以 vtable[N] 的值在 Hook 前后不变，仍然指向原始函数地址。
//   这个特性让我们能用 vtable[N] 的值做指纹去重。
//
// 【去重策略 — 为什么不能每次 Direct3DCreate9 都 Hook？】
//   游戏可能多次调用 Direct3DCreate9（例如枚举设备能力后创建、多适配器）。
//   标准 d3d9.dll：所有 IDirect3D9 实例共享同一个 VTable。
//      → vtable[8] 地址相同 → 跳过 → 不会重复 Hook
//   DXVK 等替代实现：每个实例可能有独立的 VTable 副本。
//      → vtable[8] 地址不同 → 再次 Hook → 每个实例的 VTable 都补丁
//
//   如果对同一地址重复调用 MH_CreateHook，MinHook 返回
//   MH_ERROR_ALREADY_CREATED，不会崩溃但浪费一次调用。
//   地址比较避免了不必要的 API 调用。
//
// 【多线程安全】
//   Direct3DCreate9 几乎总在主线程调用（D3D9 文档要求），但 DXVK 可能
//   从工作线程调用。s_patchedEntry* 用 std::atomic 保证安全。
//   load-then-store 的竞态最坏结果是冗余 MH_CreateHook（返回 ALREADY_CREATED），无害。
// ===================================================================
static IDirect3D9* WINAPI HkDirect3DCreate9(UINT SDKVersion) {
    IDirect3D9* pD3D9 = TrueDirect3DCreate9(SDKVersion);
    if (!pD3D9) return nullptr;  // 创建失败 → 什么都不做

    // 获取这个 IDirect3D9 实例的 VTable
    // *(void***)pD3D9: 三重指针解引用 —
    //   pD3D9 是指向 COM 对象的指针
    //   COM 对象的第一个字段是指向 VTable 的指针（void**）
    //   所以 *(void***)pD3D9 = VTable 的地址 = void**
    void** vtable = *(void***)pD3D9;

    // 已补丁的 VTable 入口地址（atomic：多线程安全）
    // MinHook 修改入口代码，不修改 VTable 指针本身
    // 因此 vtable[N] 地址值在 Hook 前后不变，可作为去重指纹
    static std::atomic<void*> s_patchedEntry8{nullptr};
    static std::atomic<void*> s_patchedEntry16{nullptr};

    // ---- VTable 索引 8: GetAdapterDisplayMode ----
    void* entry8 = vtable[8];
    if (entry8 != s_patchedEntry8.load(std::memory_order_relaxed)) {
        // 新地址（或首次）→ 安装 Hook
        if (MH_CreateHook(entry8, (void*)&HkGetAdapterDisplayMode,
                          (void**)&TrueGetAdapterDisplayMode) == MH_OK) {
            MH_EnableHook(entry8);
            s_patchedEntry8.store(entry8, std::memory_order_relaxed);
        }
    }

    // ---- VTable 索引 16: CreateDevice ----
    void* entry16 = vtable[16];
    if (entry16 != s_patchedEntry16.load(std::memory_order_relaxed)) {
        if (MH_CreateHook(entry16, (void*)&HkCreateDevice,
                          (void**)&TrueCreateDevice) == MH_OK) {
            MH_EnableHook(entry16);
            s_patchedEntry16.store(entry16, std::memory_order_relaxed);
        }
    }
    return pD3D9;
}

// ===================================================================
// UE3::Init / UE3::Shutdown / UE3::SetActive / UE3::IsActive
//
// 【Init 中的安装顺序 — 为什么先 d3d9 后 user32？】
//   这是经过实践教训后的修正。旧版先装 user32 再装 d3d9：
//     如果 d3d9 失败 → 返回 false → 但 user32 Hook 已安装且无回滚 →
//     调用方回退到 WorkerThread → Hook + 轮询共存 → 行为异常
//   新版先装 d3d9：
//     如果 d3d9 失败 → 直接 return false，无任何残留 Hook →
//     调用方干净回退到 WorkerThread
//     如果 d3d9 成功 → 再装 user32（此时 d3d9 已确认可用，安全）
//
// 【为什么每个 Hook 都检查返回值并 MH_Uninitialize？】
//   一致性：d3d9 Hook 因为 d3d9.dll 可能未加载而检查，
//   user32 Hook 也一起检查（虽然 user32.dll 始终已加载）。
//   如果未来有 Hook 失败，所有已安装的 Hook 通过 MH_Uninitialize 清理。
// ===================================================================
namespace UE3 {

    bool Init(const AppConfig& cfg) {
        // 保存配置快照（Hook 回调中通过此副本读取配置，避免跨编译单元依赖）
        g_UE3RenderWidth  = cfg.UE3RenderWidth;
        g_UE3RenderHeight = cfg.UE3RenderHeight;
        g_UE3Config       = cfg;

        // 初始化 MinHook 运行时（内部分配跳板内存等）
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            return false;  // MinHook 无法初始化 → 整体失败
        }

        // ---- 第一步：尝试安装 d3d9 Hook（风险点：d3d9.dll 可能未加载） ----
        status = MH_CreateHookApi(L"d3d9.dll", "Direct3DCreate9",
            (void*)&HkDirect3DCreate9, (void**)&TrueDirect3DCreate9);
        if (status != MH_OK) {
            // d3d9.dll 不可用 — UE3 模式无法工作
            // MH_Uninitialize 清理 MinHook 内部资源，不留残留
            MH_Uninitialize();
            return false;
        }
        MH_EnableHook(MH_ALL_HOOKS);  // 激活 d3d9 Hook

        // ---- 第二步：d3d9 成功 → 安装 user32 Hook（user32.dll 始终可用） ----
        status = MH_CreateHookApi(L"user32.dll", "SetWindowLongA",
            (void*)&HkSetWindowLongA, (void**)&TrueSetWindowLongA);
        if (status != MH_OK) { MH_Uninitialize(); return false; }
        MH_EnableHook(MH_ALL_HOOKS);

        status = MH_CreateHookApi(L"user32.dll", "SetWindowLongW",
            (void*)&HkSetWindowLongW, (void**)&TrueSetWindowLongW);
        if (status != MH_OK) { MH_Uninitialize(); return false; }
        MH_EnableHook(MH_ALL_HOOKS);

        status = MH_CreateHookApi(L"user32.dll", "SetWindowPos",
            (void*)&HkSetWindowPos, (void**)&TrueSetWindowPos);
        if (status != MH_OK) { MH_Uninitialize(); return false; }
        MH_EnableHook(MH_ALL_HOOKS);

        return true;
    }

    // Shutdown — 卸载所有 Hook 并释放 MinHook 资源
    // 安全重复调用：MH_DisableHook 和 MH_Uninitialize 是幂等的
    void Shutdown() {
        MH_DisableHook(MH_ALL_HOOKS);  // 恢复所有被 Hook 函数的原始字节
        MH_Uninitialize();             // 释放 MinHook 内部分配的内存
    }

    // SetActive — 热键切换 Hook 开关
    // relaxed 足够：不需要与其他内存操作排序，只需要值的空中继
    void SetActive(bool active) {
        g_bHooksActive.store(active, std::memory_order_relaxed);
    }

    bool IsActive() {
        return g_bHooksActive.load(std::memory_order_relaxed);
    }

} // namespace UE3
