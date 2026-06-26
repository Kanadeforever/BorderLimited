// ===================================================================
// window.cpp — 窗口操作实现
//
// 【本文件的职责】
//   所有 Win32 API 级别的窗口操作都在这里实现。main.cpp 通过
//   WindowManager 命名空间调用这些函数，不直接接触 Win32 API 细节。
//   这样分层的好处：如果将来需要支持 DXGI（D3D10/11/12），
//   只需要修改 main.cpp 的模式路由，窗口操作代码不用动。
//
// 【命名空间 WindowManager】
//   全局函数按功能分组在命名空间中，替代原版 C# 的静态类。
//   没有用类的原因：所有状态都是 static 的（进程内单例），
//   用命名空间减少一层 this 指针传递。
//
// 【对标参考】
//   原版 Borderless Gaming:
//     Manipulation.cs — 窗口修改（MakeBorderless 等）
//     Native.cs — 系统级功能（任务栏/光标/DPI）
//   SRWE (Simple Runtime Window Editor):
//     XOR 位清除技巧 + WM_EXITSIZEMOVE 启发
// ===================================================================

#include "window.h"
#include "native.h"
#include <atomic>  // std::atomic — 跨线程安全的状态标志

namespace WindowManager {

// ===================================================================
// RemoveMenuBar — 移除窗口的原生 Win32 菜单栏
//
// 【为什么始终删除位置 0？】
//   删除菜单项后，后续项的索引会向前移动。
//   例: [文件(0), 编辑(1), 帮助(2)] → 删位置0 → [编辑(0), 帮助(1)]
//   所以 for 循环中始终删位置 0 即可依次删除全部。
//   用 RemoveMenu(..., i, ...) 会在删除后索引错位（跳过一半的项）。
//
// 【不可逆警告】
//   MF_REMOVE 删除菜单项后，其句柄被销毁，无法恢复。
//   不同于 MF_DELETE（仅从菜单中移除，句柄仍存在可重新插入）。
//   MF_REMOVE 更彻底（释放内存），但窗口关闭前菜单永久丢失。
//   热键恢复只恢复窗口样式和位置，不恢复菜单栏。
//   所以 RemoveMenus=true 需谨慎使用。
// ===================================================================
static void RemoveMenuBar(HWND hwnd) {
    HMENU hMenu = ::GetMenu(hwnd);
    if (!hMenu) return;  // 窗口没有菜单栏（大部分现代游戏都没有）

    int count = ::GetMenuItemCount(hMenu);
    for (int i = 0; i < count; ++i) {
        ::RemoveMenu(hMenu, 0, MF_BYPOSITION | MF_REMOVE);
    }
    ::DrawMenuBar(hwnd);  // 重绘：让菜单栏区域消失
}

// ===================================================================
// ApplyBorderless — 将目标窗口转换为无边框全屏
//
// 【这是整个项目最核心的函数】
//   所有路径（轮询模式 + UE3 Hook 模式）最终都调用这个函数来完成
//   实际的窗口样式修改。它不负责"何时"调用（那是 main.cpp/ue3.cpp 的职责），
//   只负责"如何"修改窗口。
//
// 【执行流程 — 9 个步骤】
//   1. 验证窗口有效 → 2. 移除菜单栏 → 3. 读取当前样式
//   → 4. 位掩码清除边框位 → 5. 计算目标矩形
//   → 6. 应用偏移量 → 7. SetWindowPos + 延迟模式处理
//   → 8. 可选最大化 → 9. 可选置顶 + SendExitSizeMove
//
// 【延迟模式（DelayMs）】
//   某些游戏引擎（GameMaker 等）在创建窗口后不是立即设置最终位置，
//   而是先创建 → 短暂延迟 → 再调整。如果我们在延迟之前就设置样式，
//   引擎的后续调整会覆盖我们的修改。
//   延迟模式：先设位置 → Sleep(DelayMs) → 再设样式 → 再次 SetWindowPos
//   对标原版的 MakeWindowBorderlessDelayed 函数。
//
// 【SetWindowPos vs SetWindowLong 的区别】
//   SetWindowLongPtrW 修改窗口属性（样式位），但不触发视觉刷新。
//   SetWindowPos 修改窗口位置/大小/Z序，并触发 WM_WINDOWPOSCHANGED
//   使样式变化"生效"。两者必须配对使用。
//   不加 SWP_FRAMECHANGED → 样式改了但外观不变 → 用户看不到效果。
//
// 【参数】
//   cfg  — 完整配置（Size/Maximize/TopMost/Offsets 等）都从这里读
//   hwnd — 目标窗口句柄
//   返回: true=操作成功, false=窗口无效（已销毁/句柄错误）
// ===================================================================
bool ApplyBorderless(const AppConfig& cfg, HWND hwnd) {
    // ---- 步骤 1：验证窗口有效性 ----
    // 窗口可能在"找到"和"应用"之间被销毁（游戏关闭/切分辨率）
    if (!hwnd || !::IsWindow(hwnd)) return false;

    bool needsDelay = (cfg.DelayMs > 0);

    // ---- 步骤 2：移除菜单栏（不可逆，谨慎） ----
    if (cfg.RemoveMenus) {
        RemoveMenuBar(hwnd);
    }

    // ---- 步骤 3-4：读取 + 清除窗口样式位 ----
    // GetWindowLongPtrW: Ptr 版本兼容 32/64 位（普通版在 64 位下截断返回值）
    // 样式是 LONG 类型（32 位），类型转换安全。
    LONG styleCur   = (LONG)::GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG exStyleCur = (LONG)::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    // XOR 位清除: (value | mask) ^ mask
    // 为什么不用 value & ~mask？XOR 版本对 LONG 符号扩展免疫，更安全。
    // 参考: SRWE (Simple Runtime Window Editor) by dtzxporter
    LONG styleNew   = (styleCur   | WindowStyle::StylesToRemove)         ^ WindowStyle::StylesToRemove;
    LONG exStyleNew = (exStyleCur | WindowStyle::ExtendedStylesToRemove) ^ WindowStyle::ExtendedStylesToRemove;

    // 非延迟模式：立即设置新样式
    if (!needsDelay) {
        ::SetWindowLongPtrW(hwnd, GWL_STYLE,  styleNew);
        ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyleNew);
    }

    // ---- 步骤 5：计算目标矩形 ----
    RECT target = {};
    if (cfg.Size == WindowSize::FullScreen) {
        // 找到窗口所在的显示器，获取其完整分辨率
        // MONITOR_DEFAULTTONEAREST: 窗口大部分面积所在的显示器
        HMONITOR hMon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(MONITORINFO) };  // cbSize 必须先填充！
        ::GetMonitorInfoW(hMon, &mi);
        target = mi.rcMonitor;  // 显示器完整矩形（以虚拟屏幕坐标表示）
    }
    else if (cfg.Size == WindowSize::Custom) {
        // 用户指定的精确坐标 — 配合预览窗口拖拽定位功能
        target.left   = cfg.PositionX;
        target.top    = cfg.PositionY;
        target.right  = cfg.PositionX + cfg.PositionW;
        target.bottom = cfg.PositionY + cfg.PositionH;
    }
    else {  // Borderless：仅去边框，保持位置和大小
        ::GetWindowRect(hwnd, &target);
    }

    // ---- 步骤 6：应用四边偏移 ----
    // 正值收缩（例如 OffsetL=2 → 左边往右缩 2px，遮盖左边黑边）
    // 负值扩展（例如 OffsetR=-5 → 右边往外扩 5px，消除右边缝隙）
    target.left   += cfg.OffsetL;
    target.top    += cfg.OffsetT;
    target.right  += cfg.OffsetR;   // right 加减 → 改变宽度
    target.bottom += cfg.OffsetB;   // bottom 加减 → 改变高度

    int x = target.left;
    int y = target.top;
    int w = target.right  - target.left;
    int h = target.bottom - target.top;

    // ---- 步骤 7：设置窗口位置和大小 ----
    // SWP_FRAMECHANGED: 关键！通知窗口管理器"样式已改变，重算非客户区"
    //   不加这个标志 → 边框虽然在位掩码层面被清除了，但视觉上仍然存在
    // SWP_NOOWNERZORDER: 不改变所有者窗口（通过 Owner 关联的窗口）的 Z 序
    // SWP_NOSENDCHANGING: 不发 WM_WINDOWPOSCHANGING，减少不必要消息
    UINT flags = SWP_SHOWWINDOW | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING;
    if (cfg.Size == WindowSize::Borderless) {
        flags |= (SWP_NOMOVE | SWP_NOSIZE);  // 不移动不调整大小
    }

    if (needsDelay) {
        // ---- 延迟模式 ----
        // 1) 先调整位置和大小
        flags |= SWP_FRAMECHANGED;
        ::SetWindowPos(hwnd, nullptr, x, y, w, h, flags);
        // 2) 等待引擎完成窗口初始化（GameMaker 等引擎需要这步）
        ::Sleep(cfg.DelayMs);
        // 3) 设置样式
        ::SetWindowLongPtrW(hwnd, GWL_STYLE,  styleNew);
        ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyleNew);
        // 4) 再次 SetWindowPos 让样式生效
        //    只刷新框架，不移动不调整大小（位置已在上一步设置好了）
        ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }
    else {
        // ---- 非延迟模式（大多数游戏） ----
        // 一次性应用样式 + 位置：先设样式 → SetWindowPos 同时提交
        flags |= SWP_FRAMECHANGED;
        ::SetWindowPos(hwnd, nullptr, x, y, w, h, flags);
    }

    // ---- 步骤 8：可选最大化 ----
    // 为什么在 SetWindowPos 后调用？最大化操作需要窗口已经在正确位置。
    // Borderless 模式不最大化（保持用户指定的位置大小）。
    if (cfg.ShouldMaximize && cfg.Size != WindowSize::Borderless) {
        ::ShowWindow(hwnd, SW_MAXIMIZE);
    }

    // ---- 步骤 9：可选置顶 ----
    // HWND_TOPMOST: 窗口在所有非置顶窗口之上，包括 Alt+Tab 切换后
    // 注意：如果两个窗口都是 TOPMOST，它们之间的 Z 序正常
    if (cfg.TopMost) {
        ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOSENDCHANGING);
    }

    // ---- 步骤 9b：可选 WM_EXITSIZEMOVE ----
    // 某些老游戏引擎在收到 WM_EXITSIZEMOVE 之前不会更新渲染目标大小。
    // 发送此消息欺骗引擎："窗口调整已完成，请重新计算渲染缓冲区"。
    // 参考: SRWE 的 hotsampling 功能
    if (cfg.SendExitSizeMove) {
        ::SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
    }

    return true;
}

// ===================================================================
// 任务栏控制
//
// 【原理】
//   1. 找到任务栏窗口（类名固定为 "Shell_TrayWnd"）
//   2. ShowWindow(SW_HIDE/SW_SHOW) 控制可见性
//   3. SystemParametersInfo(SPI_SETWORKAREA) 调整桌面工作区
//      工作区 = 桌面减去任务栏占用的空间。隐藏任务栏后需要扩展工作区，
//      否则最大化窗口不会覆盖任务栏原来占用的区域。
//
// 【FindWindow 查找任务栏的可靠性】
//   "Shell_TrayWnd" 类名从 Windows 95 到 Windows 11 从未改变。
//   即使安装了第三方 Shell（如 Classic Shell），类名通常不变。
//   如果找不到（如运行在特殊终端环境），静默返回、不影响主功能。
// ===================================================================

static HWND FindTaskbar() {
    return ::FindWindowW(L"Shell_TrayWnd", nullptr);
}

// EnumDisplayMonitors 的回调：为每个显示器扩展工作区
static BOOL CALLBACK ExpandWorkAreaProc(HMONITOR hMon, HDC, LPRECT, LPARAM) {
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (GetMonitorInfoW(hMon, &mi)) {
        RECT rc = mi.rcMonitor;  // 完整显示器分辨率 = 新的工作区
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &rc, SPIF_SENDCHANGE);
        // SPIF_SENDCHANGE: 广播 WM_SETTINGCHANGE，所有窗口收到通知后自动调整布局
    }
    return TRUE;  // 继续枚举下一个显示器
}

void HideWindowsTaskbar(bool hide) {
    HWND hTaskbar = FindTaskbar();
    if (!hTaskbar) return;

    if (hide) {
        ShowWindow(hTaskbar, SW_HIDE);
        EnumDisplayMonitors(nullptr, nullptr, ExpandWorkAreaProc, 0);
    }
    else {
        ShowWindow(hTaskbar, SW_SHOW);
        // 传 nullptr → 系统自动恢复默认工作区（减去任务栏区域）
        SystemParametersInfoW(SPI_SETWORKAREA, 0, nullptr, SPIF_SENDCHANGE);
    }
}

// ===================================================================
// HideMouseCursor — 隐藏/恢复鼠标光标
//
// 【不是让光标不可见，而是替换为透明光标】
//   直接 ShowCursor(FALSE) 会设置一个内部计数器，其他程序调用
//   ShowCursor(TRUE) 就会覆盖。而且每次窗口进入/离开都会重置。
//   替换光标的方式是永久性的：系统一直有一个光标（透明的），
//   直到我们主动恢复原始光标。
//
// 【单色光标（Monochrome Cursor）的位平面格式】
//   CreateCursor 接受两个位平面（每个平面每行 WORD 对齐）：
//     AND 掩码: 1=保留屏幕像素, 0=清除为黑色
//     XOR 掩码: 1=反转像素颜色, 0=不变
//   效果 = (screen & AND) ^ XOR
//
//   透明: AND=1(保留), XOR=0(不变) → screen 不变 → 完全透明
//   黑色: AND=0(清除), XOR=0(不变) → 强制变黑
//   白色: AND=0(清除), XOR=1(反转) → 先变黑再反转 = 变白
//   反转: AND=1(保留), XOR=1(反转) → 像素颜色反转
//
//   1x1 光标每平面 = 2 字节（1 行 × 1 像素，WORD 对齐使行宽=2）
//   AND: {0x80, 0x00} → 第 1 行第 1 像素 = 1 → 保留屏幕像素
//   XOR: {0x00, 0x00} → 第 1 行第 1 像素 = 0 → 不反转
//   结果：光标位置处的 1 个像素完全透明，肉眼看不到光标。
//
// 【SetSystemCursor 的所有权转移】
//   调用成功后系统取得光标句柄的所有权，会自动管理其生命周期。
//   不能再次 DestroyCursor — 系统会在需要时自行销毁。
//   这就是为什么 g_hBlankCursor 在隐藏后不调用 DestroyCursor、
//   在恢复后直接设为 nullptr 的原因。
//
// 【CAS 原子切换】
//   两个线程（WorkerThread 首次窗口 + UE3 D3D9 回调首次设备）
//   可能同时调用 HideMouseCursor(true)。CAS 确保只有一个线程进入
//   隐藏逻辑，另一个线程看到"已经是目标状态"而跳过。
//   这防止了 g_hOriginalCursor 被覆盖导致句柄泄漏。
// ===================================================================

static HCURSOR           g_hOriginalCursor = nullptr;
static HCURSOR           g_hBlankCursor    = nullptr;
static std::atomic<bool> g_bCursorHidden{false};   // 当前光标状态

#ifndef OCR_NORMAL       // WIN32_LEAN_AND_MEAN 可能不定义此常量
#define OCR_NORMAL 32512  // 标准箭头光标的系统对象索引
#endif

void HideMouseCursor(bool hide) {
    // CAS 原子切换 — 只有一个线程能执行状态转换
    bool expected = !hide;
    if (!g_bCursorHidden.compare_exchange_strong(expected, hide))
        return;  // 另一个线程已经完成了切换

    if (hide) {
        // ---- 隐藏光标 ----
        // 1) 保存当前系统箭头光标用于恢复
        //    LoadCursor 返回共享句柄（不拥有），必须用 CopyIcon 复制一份
        //    自己的副本，否则原始光标被替换后无从恢复
        HCURSOR hDefault = ::LoadCursor(nullptr, IDC_ARROW);
        g_hOriginalCursor = (HCURSOR)::CopyIcon((HICON)hDefault);

        // 2) 创建 1x1 透明光标
        BYTE andPlane[2] = {0x80, 0x00};  // 保留屏幕像素（不涂黑）
        BYTE xorPlane[2] = {0x00, 0x00};  // 不反转颜色
        g_hBlankCursor = ::CreateCursor(nullptr, 0, 0, 1, 1, andPlane, xorPlane);

        // 3) 替换系统箭头光标
        if (g_hBlankCursor) {
            // SetSystemCursor 成功后系统持有 g_hBlankCursor 的所有权
            SetSystemCursor(g_hBlankCursor, OCR_NORMAL);
        }
    }
    else {
        // ---- 恢复光标 ----
        // SetSystemCursor 销毁当前光标并替换为 g_hOriginalCursor
        // 成功后 g_hOriginalCursor 所有权转移给系统 → 设为 nullptr
        if (g_hOriginalCursor) {
            SetSystemCursor(g_hOriginalCursor, OCR_NORMAL);
            g_hOriginalCursor = nullptr;
        }
        // g_hBlankCursor 已在 SetSystemCursor 调用时被系统销毁
        g_hBlankCursor = nullptr;
    }
}

// ===================================================================
// OverrideDpiScaling — 禁用 Windows DPI 虚拟化缩放
//
// 【问题背景】
//   Windows 在高 DPI 屏幕（4K 等）上会自动放大不支持 DPI 感知的老程序。
//   放大通过 GPU 缩放实现（类似放大镜），结果看起来"模糊"。
//   游戏通常会自行处理分辨率，不需要 Windows 的"帮助"。
//   此项禁用 Windows 的 DPI 虚拟化，让游戏以原始像素渲染。
//
// 【三层回退策略 — 从新到旧】
//   不是所有 Windows 版本都支持最新的 DPI API。
//   从最新到最老逐级尝试，第一级成功就 return。
//
//   方案 1: SetProcessDpiAwarenessContext (Win10 1607+)
//     PER_MONITOR_AWARE_V2: 每个显示器独立 DPI，窗口移动时动态切换。
//     支持子窗口不同的 DPI 缩放模式。是最佳选择。
//     (HANDLE)(-4) = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
//
//   方案 2: SetProcessDpiAwareness (Win8.1+)
//     PER_MONITOR_AWARE: 每个显示器独立 DPI，但不支持 V2 高级特性。
//     需要 LoadLibrary → GetProcAddress → FreeLibrary（动态加载，
//     shcore.dll 可能不存在于老系统）。
//
//   方案 3: SetProcessDPIAware (Vista/7)
//     最老的 API，系统级 DPI 感知（全显示器统一 DPI）。
//     不需要动态加载（user32.dll 始终可用）。
//
// 【副作用】
//   禁用缩放后，800x600 的游戏在 4K 屏上只占屏幕的一小部分，
//   不会自动填充全屏。用户需要自行设置游戏内分辨率。
//
// 【为什么检查返回值？】
//   SetProcessDpiAwareness 可能返回 E_ACCESSDENIED —
//   如果进程启动时通过清单文件已经设置了 DPI 感知模式，
//   后续调用任何 DPI API 都会失败。忽略返回值会导致
//   误以为成功 → 跳过方案 3 → 实际 DPI 策略可能未生效。
// ===================================================================

#ifndef PROCESS_PER_MONITOR_DPI_AWARE
#define PROCESS_PER_MONITOR_DPI_AWARE 2
#endif

void OverrideDpiScaling() {
    // ---- 方案 1：Windows 10 1607+ ----
    {
        HMODULE hUser32 = ::GetModuleHandleW(L"user32.dll");
        // GetModuleHandle 不需要 FreeLibrary（user32 始终加载）
        typedef BOOL(WINAPI* Fn)(HANDLE);
        Fn fn = hUser32 ? (Fn)::GetProcAddress(hUser32, "SetProcessDpiAwarenessContext") : nullptr;
        if (fn) {
            fn((HANDLE)(-4));  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            return;
        }
    }

    // ---- 方案 2：Windows 8.1+ ----
    {
        HMODULE hShcore = ::LoadLibraryW(L"shcore.dll");
        if (hShcore) {
            typedef HRESULT(WINAPI* Fn)(int);
            Fn fn = (Fn)::GetProcAddress(hShcore, "SetProcessDpiAwareness");
            if (fn && SUCCEEDED(fn(PROCESS_PER_MONITOR_DPI_AWARE))) {
                ::FreeLibrary(hShcore);
                return;     // 成功 → 跳过方案 3
            }
            ::FreeLibrary(hShcore);
            // 不 return：失败/函数不存在 → 继续回退到方案 3
        }
    }

    // ---- 方案 3：Vista/7（最终回退） ----
    ::SetProcessDPIAware();
}

// ===================================================================
// 光标锁定 — ClipCursor
//
// 【ClipCursor 的工作原理】
//   设置鼠标光标的活动范围矩形（屏幕坐标）。光标无法移出此矩形。
//   这是硬件级别的限制 — 即使用户快速甩鼠标，光标也被"锁"在矩形内。
//   调用 ClipCursor(NULL) 解除所有限制。
//
// 【为什么用 GetWindowRect 而不是 GetClientRect？】
//   ClipCursor 需要屏幕坐标。GetWindowRect 返回窗口左上角和右下角
//   的屏幕坐标，正好是 ClipCursor 需要的格式。
//   GetClientRect 返回客户区坐标（相对窗口自己的左上角），需要额外转换。
//   无边框后客户区 = 整个窗口矩形，两者等价。
// ===================================================================
void LockCursorToWindow(HWND hwnd) {
    RECT rect;
    if (::GetWindowRect(hwnd, &rect)) {
        ::ClipCursor(&rect);
    }
}

void UnlockCursor() {
    ::ClipCursor(nullptr);
}

// ===================================================================
// Win 键禁用 — 低层键盘钩子 (WH_KEYBOARD_LL)
//
// 【为什么用 WH_KEYBOARD_LL 而不是 RegisterHotKey？】
//   RegisterHotKey 只能注册组合键作为"全局快捷键"，无法阻止系统
//   处理 Win 键。Win 键的"开始菜单弹出"在 RegisterHotKey 之前被系统处理。
//   WH_KEYBOARD_LL 可以拦截所有键盘事件（包括系统快捷键），并"吞掉"
//   事件（返回 1 = 不传递给后续钩子/目标窗口）。
//
// 【为什么需要独立的钩子线程？】
//   WH_KEYBOARD_LL 要求安装钩子的线程必须有消息泵（GetMessage 循环）。
//   DllMain 线程不能运行消息泵（会阻塞加载过程）。
//   所以在独立线程中安装钩子并运行消息泵。
//
// 【为什么只拦 KeyDown 不拦 KeyUp？】
//   我们只关心"防止开始菜单弹出"，这只需要拦截 KeyDown 事件。
//   如果同时拦截 KeyUp：GetAsyncKeyState(VK_LWIN) 永远报告"按下中"。
//   有些游戏/输入库用 GetAsyncKeyState 检测按键状态，误判会影响游戏。
//   放行 KeyUp 让系统知道 Win 键已释放，GetAsyncKeyState 正确清空。
//   这解决了旧版中 BUG 10 的问题。
//
// 【为什么 WinKeyProc 检查 wParam == WM_KEYDOWN？】
//   WH_KEYBOARD_LL 的 wParam 是 WM_KEYDOWN/WM_KEYUP/WM_SYSKEYDOWN 等。
//   检查 wParam 确保只拦截"真实按下"，不误拦其他键盘消息。
//   Win 键单独按下永远只产生 WM_KEYDOWN（不是 WM_SYSKEYDOWN）。
//
// 【WM_QUIT 线程退出机制】
//   StopWinKeyHook 通过 PostThreadMessage(WM_QUIT) 通知钩子线程退出。
//   GetMessage 收到 WM_QUIT 返回 0 → while 循环退出 → 线程正常结束。
//   然后 WaitForSingleObject 等待线程完全退出后再 CloseHandle。
//   这解决了旧版中线程句柄泄漏的问题。
// ===================================================================

static HHOOK               g_hWinKeyHook = nullptr;
static HANDLE              g_hHookThread = nullptr;
static std::atomic<DWORD>  g_dwHookThreadId{0};  // HookThreadProc 写，StopWinKeyHook 读

static LRESULT CALLBACK WinKeyProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {  // HC_ACTION: 正常钩子消息（非异常）
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        // 只拦截左/右 Win 键的按下事件
        if ((p->vkCode == VK_LWIN || p->vkCode == VK_RWIN) &&
            wParam == WM_KEYDOWN) {
            return 1;  // 返回 1 = "此事件已被处理，不要继续传递"
                       // 结果: 开始菜单不会弹出，游戏收不到 Win 键
        }
    }
    // 其他所有情况 → 传递给下一个钩子/目标窗口
    return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static DWORD WINAPI HookThreadProc(LPVOID) {
    // 保存线程 ID，StopWinKeyHook 需要向这里发送 WM_QUIT
    g_dwHookThreadId.store(::GetCurrentThreadId(), std::memory_order_relaxed);

    // 获取 DLL 自身模块句柄（钩子过程 WinKeyProc 在这个 DLL 内）
    HMODULE hOurModule = nullptr;
    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&HookThreadProc,  // 用自身函数地址定位模块
        &hOurModule);

    // 安装全局低级键盘钩子
    g_hWinKeyHook = ::SetWindowsHookExW(
        WH_KEYBOARD_LL,     // 低级键盘钩子 — 可拦截系统快捷键
        WinKeyProc,         // 钩子回调函数
        hOurModule,         // 回调函数所在的 DLL 模块
        0);                 // 0 = 全局钩子（拦截所有线程的键盘事件）
    if (!g_hWinKeyHook) return 1;  // 安装失败 → 线程退出

    // 消息泵：钩子需要消息循环来接收键盘事件
    // GetMessage 阻塞等待，有消息时处理，WM_QUIT 时返回 0 退出
    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return 0;
}

void StartWinKeyHook() {
    if (g_hHookThread) return;  // 已运行，避免重复创建
    g_hHookThread = ::CreateThread(nullptr, 0, HookThreadProc, nullptr, 0, nullptr);
}

void StopWinKeyHook() {
    // 1) 卸载钩子（停止拦截键盘事件）
    if (g_hWinKeyHook) {
        ::UnhookWindowsHookEx(g_hWinKeyHook);
        g_hWinKeyHook = nullptr;
    }

    // 2) 向钩子线程发送退出信号
    //    exchange(0) 原子地读取并清零 → 确保只发送一次 WM_QUIT
    DWORD tid = g_dwHookThreadId.exchange(0, std::memory_order_relaxed);
    if (tid) {
        ::PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }

    // 3) 等待线程退出 + 清理句柄
    if (g_hHookThread) {
        ::WaitForSingleObject(g_hHookThread, 2000);  // 最多等 2 秒
        ::CloseHandle(g_hHookThread);
        g_hHookThread = nullptr;
    }
}

// ===================================================================
// 窗口原始状态保存/恢复 — 热键切换的基础设施
//
// 【设计理念 — 为什么每次覆盖而不是只保存一次？】
//   旧版用 g_saved 标志确保"只保存一次"。但有一个问题：
//   如果首先检测到的是启动画面（Splash），保存了启动画面的样式和位置，
//   后续真正的游戏主窗口出现时不会重新保存 → 恢复时恢复到启动画面的状态。
//   新版每次调用都覆盖：WorkerThread 每次慢路径都保存最新的窗口状态。
//   在 HotkeyThread 调用 RestoreWindow 时，恢复的是"最近一次保存的"状态。
//
// 【线程安全 — acquire/release 协议】
//
//   写入侧（SaveOriginalState — WorkerThread/UE3 回调）：
//     1. g_savedStateReady.store(false, relaxed)      ← 标记"正在写入"
//     2. 写 g_savedStyle / g_savedExStyle / g_savedRect ← 实际数据
//     3. g_savedStateReady.store(true, release)       ← 标记"写入完成"
//     release 保证：步骤 2 的所有写入在步骤 3 之前对所有线程可见
//
//   读取侧（RestoreWindow — HotkeyThread）：
//     1. g_savedStateReady.load(acquire)             ← 检查"是否可读"
//     acquire 保证：如果读到 true，步骤 2-3 的写入一定可见
//     2. 拷贝三个字段到本地变量                        ← 避免后续覆盖影响
//     3. g_savedStateReady.load(acquire) 二次检查     ← TOCTOU 防护
//
//   【TOCTOU 双检】
//    第 1 次 acquire 通过后，拷贝三个字段期间 SaveOriginalState 可能
//    开始了一轮新写入（步骤 1 设 false）。拷贝完成后第 2 次 acquire：
//    - 如果仍是 true → 拷贝期间没有并发写入 → 数据一致 → 使用
//    - 如果变成 false → 拷贝期间有并发写入 → 数据可能混合 → 丢弃本次
//
//   【relaxed 用于 store(false)】
//    没有线程会 acquire 读到 false 后继续操作（RestoreWindow 读到 false
//    直接 return）。relaxed 足够，不需要同步开销。
// ===================================================================

static std::atomic<bool> g_savedStateReady{false};
static LONG  g_savedStyle   = 0;    // 保存的原始 GWL_STYLE（含边框位）
static LONG  g_savedExStyle = 0;    // 保存的原始 GWL_EXSTYLE
static RECT  g_savedRect    = {};   // 保存的原始窗口位置和大小

void SaveOriginalState(HWND hwnd) {
    if (!hwnd) return;
    // 步骤 1: 标记"正在写入"（relaxed：无消费者需要 acquire 读 false）
    g_savedStateReady.store(false, std::memory_order_relaxed);
    // 步骤 2: 写入数据
    g_savedStyle   = (LONG)::GetWindowLongPtrW(hwnd, GWL_STYLE);
    g_savedExStyle = (LONG)::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ::GetWindowRect(hwnd, &g_savedRect);
    // 步骤 3: 标记"写入完成"（release：保证步骤 2 对 acquire 读者可见）
    g_savedStateReady.store(true, std::memory_order_release);
}

bool IsValidSavedState() {
    // acquire 保证看到 release 写入的完整数据
    // 不再检查 g_savedStyle!=0：窗可能本来就无边框（style=0 合法）
    return g_savedStateReady.load(std::memory_order_acquire) &&
           g_savedRect.right  > g_savedRect.left &&
           g_savedRect.bottom > g_savedRect.top;
}

bool RestoreWindow(HWND hwnd) {
    // ---- 第一次检查：有保存的状态吗？ ----
    if (!hwnd || !g_savedStateReady.load(std::memory_order_acquire))
        return false;

    // ---- 拷贝到本地变量（脱离全局变量的"当前值"） ----
    // 拷贝之后，即使 SaveOriginalState 覆盖了全局变量，本地副本不变
    LONG savedStyle   = g_savedStyle;
    LONG savedExStyle = g_savedExStyle;
    RECT savedRect    = g_savedRect;

    // ---- 第二次检查（TOCTOU 双检）：拷贝期间有并发写入吗？ ----
    // 如果 SaveOriginalState 在拷贝期间store(false)，这里读到false → 丢弃
    if (!g_savedStateReady.load(std::memory_order_acquire))
        return false;

    // ---- 验证数据有效性 ----
    if (!(savedRect.right > savedRect.left && savedRect.bottom > savedRect.top))
        return false;

    // ---- 恢复原始样式和位置 ----
    ::SetWindowLongPtrW(hwnd, GWL_STYLE,  savedStyle);
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, savedExStyle);
    ::SetWindowPos(hwnd, nullptr,
        savedRect.left, savedRect.top,
        savedRect.right  - savedRect.left,
        savedRect.bottom - savedRect.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    return true;
}

} // namespace WindowManager
