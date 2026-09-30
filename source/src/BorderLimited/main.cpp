// ===================================================================
// main.cpp — BorderLimited ASI 插件入口点和主控制逻辑
//
// 【本文件的职责】
//   本文件是整个插件的"大脑"。它不直接操作窗口（交给 window.cpp），
//   也不直接挂钩 API（交给 ue3.cpp），而是负责：
//     1. DllMain — ASI 加载/卸载入口
//     2. 配置加载 — 从 BorderLimited.ini 读取所有设置
//     3. 日志系统 — OutputDebugString + UTF-8 文件日志
//     4. 线程调度 — 创建/管理/销毁 HotkeyThread 和 WorkerThread
//     5. 热键检测 — 100ms 周期轮询 GetAsyncKeyState（PollToggleKey）
//     6. 模式路由 — UE3Mode=0 → 轮询路径 / UE3Mode=1 → Hook 路径
//
// 【线程架构 — 为什么要两个线程？】
//   旧版本把热键检测和窗口维护混在一个 WorkerThread 里，
//   导致 UE3 模式（不创建 WorkerThread）下热键完全失效。
//   新架构拆成两个独立线程：
//
//     HotkeyThread   100ms 周期  双模式共用  只做 GetAsyncKeyState 检测
//     WorkerThread   Nms 周期   仅轮询模式  只做 ApplyToMainWindow
//
//   拆分理由：
//     - UE3 模式不需要 WorkerThread（Hook 已经在 API 层拦截），
//       但用户仍然需要热键来切换 Hook 开关
//     - 两个线程职责分离后，各自独立挂起/退出，互不影响
//     - 即使 PollingIntervalMs=0 单次模式，热键仍然可用
//
// 【线程间通信 — 所有共享变量都是 std::atomic】
//   g_hLastWindow:  HotkeyThread 读 ← WorkerThread 写 / UE3 D3D9 回调写
//   g_bRunning:     HotkeyThread 读 + WorkerThread 读 ← DllMain 写（退出信号）
//   g_bHooksActive: HotkeyThread 写（SetActive）→ D3D9 回调线程读
//   g_savedState*:  HotkeyThread 读（RestoreWindow）← WorkerThread/UE3写
//   所有原子操作用 relaxed（不需要顺序保证，只需要可见性）
//
// 【对标参考】
//   原版 BorderlessGaming: BorderlessGaming.exe + ProcessWatcher.cs + MainWindow.cs
//   线程模型参考 DSOpt 的 PollToggleKey 状态机
// ===================================================================

#include <windows.h>
#include <string>
#include <cstdarg>     // va_list, va_start, va_end
#include <atomic>      // std::atomic — 跨线程无锁共享的基础设施
#include "config.h"
#include "window.h"
#include "native.h"
#include "ue3.h"

// ===================================================================
// 全局状态变量 — 线程架构的"共享内存"
//
// 【线程安全约定】
//   - 标记为 std::atomic 的变量：任意线程可安全读写
//   - 标记为 static 的普通变量：仅在单线程内访问（见各变量注释）
//   - HANDLE 类型：仅在 DllMain 线程读写（创建和关闭），其他线程不碰
//
// 【g_hLastWindow 为什么不是 static？】
//   ue3.cpp 需要 extern 访问来在 HkCreateDevice 中设置窗口句柄。
//   如果声明为 static，链接器会把它限制在本编译单元内，ue3.cpp 无法引用。
//   所以去掉了 static，用 std::atomic<HWND> 保证跨线程安全。
// ===================================================================
static AppConfig            g_Config;                    // 运行时配置（DllMain 加载一次，之后各线程只读）
std::atomic<HWND>           g_hLastWindow{nullptr};     // 游戏主窗口句柄（非 static：ue3.cpp extern 引用）
static std::atomic<bool>    g_bRunning(true);           // 全局退出信号（true=运行, false=请求退出所有线程）
static std::wstring         g_LogPath;                  // 日志文件路径（DllMain 设置一次，各线程只读）
static HANDLE               g_hWorkerThread = nullptr;  // 轮询线程句柄（仅非 UE3 模式创建）
static HANDLE               g_hHotkeyThread = nullptr;  // 热键线程句柄（双模式都创建）

// ===================================================================
// Log — 日志输出函数
//
// 【设计决策 — 为什么同时写两个目标？】
//   OutputDebugString: 开发者用 DebugView 实时查看，无需打开文件
//   文件日志:          普通用户排查问题，UTF-8 编码任何编辑器可读
//
// 【为什么用 _vsnwprintf_s + _TRUNCATE 而不是简单的 wprintf？】
//   _TRUNCATE 保证缓冲区永远不会溢出 — 超长消息会被截断但不会崩溃。
//   在 ASI 插件中崩溃意味着拖垮整个游戏进程，所以防御性编程是必须的。
//
// 【为什么用 WideCharToMultiByte 转 UTF-8 而不是直接写宽字符？】
//   直接写 wchar_t 字节流会产生 UTF-16 LE 文件，Windows 记事本可以读
//   但其他工具（vim、cat、GitHub 在线查看）会显示乱码。UTF-8 是通用标准。
//
// 【缓冲区大小考量】
//   msg[1024]: 单条日志消息上限（_vsnwprintf_s 格式化后）
//   line[1536]: msg + 时间戳前缀 + \r\n ≈ 1024 + 30 = 足够
//   utf8[8192]: 1536 宽字符 × 4 字节(CJK/Emoji最坏情况) = 6144，余量 2KB
// ===================================================================
static void Log(const wchar_t* fmt, ...) {
    // ---- 第一步：格式化消息到宽字符串 ----
    wchar_t msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, args);
    va_end(args);

    // ---- 第二步：输出到调试器（始终执行，不依赖配置） ----
    ::OutputDebugStringW(msg);
    ::OutputDebugStringW(L"\r\n");

    // ---- 第三步：输出到文件（受 EnableLog 开关控制） ----
    // 为什么检查 EnableLog 在两个地方（这里 + DllMain 清空日志时）？
    //   这里控制"是否写入新日志"；DllMain 控制"是否清空旧日志"。
    //   如果用户设 EnableLog=0，旧日志保留不删，新日志不写。
    if (g_Config.EnableLog && !g_LogPath.empty()) {
        // FILE_APPEND_DATA: 每次写操作自动定位到文件末尾（原子追加）
        // FILE_SHARE_READ:  允许用户用记事本打开日志文件的同时插件继续写入
        HANDLE hFile = ::CreateFileW(
            g_LogPath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ,
            nullptr,
            OPEN_ALWAYS,             // 文件存在→打开；不存在→创建
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            // 构造 "[2026-06-26 19:30:45.123] 消息内容\r\n" 格式
            SYSTEMTIME st;
            ::GetLocalTime(&st);
            wchar_t line[1536];
            int wlen = _snwprintf_s(line, _TRUNCATE,
                L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\r\n",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                msg);
            if (wlen > 0) {
                // 宽字符→UTF-8 字节流
                // sizeof(utf8)-1 传给 cbMultiByte 防止溢出（即使函数截断也不会崩溃）
                char utf8[8192];
                int utf8len = ::WideCharToMultiByte(CP_UTF8, 0, line, wlen,
                    utf8, sizeof(utf8) - 1, nullptr, nullptr);
                if (utf8len > 0) {
                    DWORD written = 0;
                    ::WriteFile(hFile, utf8, (DWORD)utf8len, &written, nullptr);
                }
            }
            ::CloseHandle(hFile);
        }
    }
}

// ===================================================================
// GetModuleDir — 获取此 DLL 所在的目录路径
//
// 【为什么不用 GetModuleFileNameW(GetModuleHandleW(nullptr), ...)？】
//   在 DLL 中 GetModuleHandleW(nullptr) 返回的是 EXE 的句柄，
//   不是 DLL 的句柄。我们需要 DLL 的路径来定位 INI 和日志文件。
//
// 【技术原理 — GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS】
//   这是一种"反向查找"技巧：给一个 DLL 内部任意地址（这里用 g_LogPath），
//   系统从地址反查它属于哪个加载的模块，返回该模块的 HMODULE。
//   UNCHANGED_REFCOUNT 表示不需要增加引用计数（避免不必要的 DLL 锁定）。
//   这在 kernel32.dll 的 GetModuleHandleEx 中实现，所有 Windows 版本可用。
//
// 【为什么截去文件名？】
//   GetModuleFileNameW 返回 "C:\Game\BorderLimited.asi"
//   我们需要的是目录 "C:\Game\"，用于拼接日志和 INI 路径
// ===================================================================
static std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH] = {};
    HMODULE hMod = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&g_LogPath,   // 这个变量的地址在 DLL 内部 → 系统反查模块
            &hMod)) {
        return L"";
    }
    ::GetModuleFileNameW(hMod, path, MAX_PATH);
    // wcsrchr 从右往左找最后一个 '\\'，截断后得到目录路径
    wchar_t* sep = wcsrchr(path, L'\\');
    if (sep) *sep = L'\0';
    return std::wstring(path);
}

// ===================================================================
// ApplyToMainWindow — 核心轮询逻辑：查找并处理游戏主窗口
//
// 【设计思想 — 为什么用两阶段策略？】
//   大多数游戏只有一个主窗口，在启动后很快创建、之后一直存在。
//   如果每次轮询都重新枚举所有窗口（慢路径），CPU 开销大且不必要。
//   所以设计"快速路径 + 慢路径回退"：
//
//     快速路径（99% 的情况）：检查已知窗口 g_hLastWindow
//       如果窗口还在且已无边框 → 几乎零开销（一次 IsWindow + 位掩码比较）
//       如果窗口还在但样式被恢复 → 重新应用（只调 ApplyBorderless）
//       如果窗口被销毁了 → 进入慢路径
//
//     慢路径（1% 的情况）：全量枚举
//       游戏切换分辨率/重建窗口时触发
//       EnumWindows → EnumThreadWindows 双回退
//
// 【线程安全】
//   本函数仅在 WorkerThread 中调用（单线程），所以内部 static 变量安全。
//   但 g_hLastWindow 是 atomic（HotkeyThread 也在读），所以用 load/store。
// ===================================================================
static void ApplyToMainWindow() {
    // ================================================================
    // 第一阶段：快速路径 — 检查已经知道的窗口
    // ================================================================
    // relaxed 足够：我们只需要 HWND 值的可见性，不需要与其他操作的顺序保证
    HWND hLast = g_hLastWindow.load(std::memory_order_relaxed);
    if (hLast != nullptr) {
        // IsWindow: 检查句柄是否仍然有效（窗口未被销毁）
        if (::IsWindow(hLast)) {
            // IsAlreadyBorderless: 读 GWL_STYLE 和 GWL_EXSTYLE，做位掩码比较
            // 如果已无边框 → 什么都不做，只更新光标锁定位置（窗口可能移动了）
            if (NativeUtil::IsAlreadyBorderless(hLast)) {
                if (g_Config.LockCursor) {
                    WindowManager::LockCursorToWindow(hLast);
                }
                return;  // ← 最常见的执行路径：零操作返回
            }
            // 窗口还在但样式不对 — 游戏引擎（如 UE3、Unity）可能调用
            // SetWindowLong 恢复了原始样式。重新应用无边框。
            Log(L"[BL] Styles restored, re-applying to hwnd=0x%p", hLast);
            WindowManager::ApplyBorderless(g_Config, hLast);
            if (g_Config.LockCursor) {
                WindowManager::LockCursorToWindow(hLast);
            }
            return;
        }
        // IsWindow 返回 false：窗口已被销毁
        // 常见场景：游戏切换分辨率、切换全屏/窗口模式、加载新关卡
        Log(L"[BL] Saved window 0x%p was destroyed, re-scanning...", hLast);
        // 窗口没了 → 解锁光标（旧窗口的裁剪矩形已无效）
        if (g_Config.LockCursor) {
            WindowManager::UnlockCursor();
        }
        // 清空保存的句柄 → 下次循环进入慢路径重新搜索
        g_hLastWindow.store(nullptr, std::memory_order_relaxed);
    }

    // ================================================================
    // 第二阶段：慢路径 — 枚举所有窗口搜索游戏主窗口
    // ================================================================
    // FindProcessMainWindow 内部有两层回退：
    //   策略1: EnumWindows（标准方式，适用于 99% 的游戏）
    //   策略2: EnumThreadWindows（某些游戏的窗口层级不常规）
    HWND hwnd = NativeUtil::FindProcessMainWindow();
    if (!hwnd) {
        // 还没找到窗口 — 游戏可能还在加载/显示启动画面
        // 每 30 秒记一条日志，防止日志文件被 "waiting..." 刷到几 MB
        static DWORD lastLogTime = 0;
        DWORD now = ::GetTickCount();
        if (now - lastLogTime > 30000) {
            Log(L"[BL] No visible captioned window found yet (waiting...)");
            lastLogTime = now;
        }
        return;
    }

    // 记录窗口信息用于排查问题
    std::wstring className = NativeUtil::GetWindowClassName(hwnd);
    std::wstring title     = NativeUtil::GetWindowTitle(hwnd);
    Log(L"[BL] New window detected: class=\"%s\" title=\"%s\" hwnd=0x%p",
        className.c_str(), title.c_str(), hwnd);

    // ================================================================
    // 首次找到窗口时的一次性系统级设置
    //
    // 【为什么用 compare_exchange_strong 而不是简单的 bool + if？】
    //   虽然当前 ApplyToMainWindow 只在 WorkerThread 单线程中调用，
    //   但 CAS 是零成本的防御性编程：即使将来有多线程调用也不会出错。
    //   CAS 保证"只有第一个到达的线程执行初始化"，后续线程跳过。
    //
    // 【为什么任务栏隐藏和光标隐藏放在这里而不是 DllMain？】
    //   因为它们需要游戏窗口存在之后才有意义。
    //   任务栏隐藏太早 → 用户可能还没看到游戏就桌面变化了
    //   光标隐藏太早 → 游戏加载画面期间鼠标消失会让用户困惑
    // ================================================================
    static std::atomic<bool> s_firstWindowDone{false};
    bool expected = false;
    if (s_firstWindowDone.compare_exchange_strong(expected, true)) {
        if (g_Config.HideWindowsTaskbar) {
            WindowManager::HideWindowsTaskbar(true);
            Log(L"[BL] Taskbar hidden");
        }
        if (g_Config.HideMouseCursor) {
            WindowManager::HideMouseCursor(true);
            Log(L"[BL] Mouse cursor hidden");
        }
    }

    // ---- 保存原始窗口状态（热键恢复时需要"恢复成什么样"的信息） ----
    // 必须在 ApplyBorderless 之前调用 — 否则保存的是无边框状态，
    // 恢复时就"恢复"成无边框（没用）
    WindowManager::SaveOriginalState(hwnd);

    // ---- 更新全局窗口句柄 — 供快速路径 + HotkeyThread 使用 ----
    g_hLastWindow.store(hwnd, std::memory_order_relaxed);

    // ---- 执行核心操作：去边框 + 调整大小/位置 ----
    bool ok = WindowManager::ApplyBorderless(g_Config, hwnd);
    Log(L"[BL] ApplyBorderless returned %s", ok ? L"true" : L"false");

    // ---- 锁定光标到新窗口 ----
    if (ok && g_Config.LockCursor) {
        WindowManager::LockCursorToWindow(hwnd);
        Log(L"[BL] Cursor locked to window");
    }
}

// ===================================================================
// PollToggleKey — 热键检测与切换（100ms 周期调用）
//
// 【状态机设计 — 为什么要两阶段？】
//   如果只用简单的"按键按下→切换"，会出现一个问题：
//   用户按住 Alt+F10 不放，每 100ms 检测到一次"按下"，
//   窗口在无边框/原始状态之间疯狂切换闪烁。
//   两阶段状态机解决这个问题：
//
//     IDLE(0) — 等待按键按下
//       ↓ 检测到 hotkey+mod 都按下
//     PRESSED(1) — 等待按键松开
//       ↓ 检测到 hotkey 松开
//     执行切换 → 回到 IDLE
//
//   这样用户按住多久都只触发一次切换。
//
// 【冷却机制 — 为什么在操作前设置 s_lastToggleTime？】
//   如果把冷却计时放在操作成功之后，那么操作本身耗时（ApplyBorderless
//   可能要 Sleep DelayMs 毫秒）期间，下一次 PollToggleKey 可能再次触发。
//   放在操作之前 + 冷却窗口覆盖操作耗时 → 有效防止双击。
//
// 【为什么检查 ForegroundWindow？】
//   用户在浏览器/IDE 中按 Alt+F10 不应该触发游戏的切换。
//   只有游戏窗口在前台时才响应热键。
//   离开前台时重置状态机 → 防止"在游戏中按下 Alt+F10，
//   切到浏览器松开 F10，切回来"这种跨窗口误触发。
// ===================================================================
static bool g_borderlessActive = false;  // 轮询模式下的无边框状态追踪（仅 HotkeyThread 访问）

static void PollToggleKey() {
    // ---- 快速退出：热键功能未启用 ----
    if (!g_Config.ToggleHotKey || g_Config.ToggleHotKeyCode == 0) return;

    // ---- 读取窗口句柄（atomic relaxed：只需值的可见性） ----
    HWND hLast = g_hLastWindow.load(std::memory_order_relaxed);
    if (!hLast) return;  // 窗口还没找到，不检测热键

    // ---- 状态机变量（static：在 PollToggleKey 调用之间保持状态） ----
    static int s_state = 0;             // 0=IDLE(等按下), 1=PRESSED(等松开)
    static DWORD s_lastToggleTime = 0;  // 上次切换的 TickCount（冷却用）

    // ---- 前台窗口检查 ----
    if (::GetForegroundWindow() != hLast) {
        s_state = 0;  // 不重置冷却 — 用户切出去再切回来不应该绕开冷却
        return;
    }

    // ---- 读取按键状态 ----
    // GetAsyncKeyState 的返回值第 15 位（0x8000）表示"此键当前被按下"。
    // 不同于 GetKeyState（需要消息队列），Async 版本直接查询硬件状态，
    // 适合在轮询线程中使用（轮询线程没有消息队列）。
    bool down = (::GetAsyncKeyState(g_Config.ToggleHotKeyCode) & 0x8000) != 0;

    // ---- 检测修饰键（只有 INI 中配置的组合才生效） ----
    // MOD_ALT/MOD_CONTROL/MOD_SHIFT 是位掩码，可以组合，例如
    // ToggleHotKeyMod=3 表示需要同时按住 Alt+Ctrl
    bool modOk = true;
    if (g_Config.ToggleHotKeyMod & MOD_ALT)     modOk = modOk && (::GetAsyncKeyState(VK_MENU)    & 0x8000);
    if (g_Config.ToggleHotKeyMod & MOD_CONTROL) modOk = modOk && (::GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (g_Config.ToggleHotKeyMod & MOD_SHIFT)   modOk = modOk && (::GetAsyncKeyState(VK_SHIFT)   & 0x8000);
    // 注意: MOD_WIN 不在这里检查，因为 WH_KEYBOARD_LL 钩子已经拦截了 Win 键，
    // GetAsyncKeyState(VK_LWIN) 在钩子只拦 KeyDown 的情况下仍然可用。

    // ---- 状态机主逻辑 ----
    switch (s_state) {
    case 0: // IDLE — 等待热键按下
        if (down && modOk) s_state = 1;  // 进入 PRESSED 状态
        break;

    case 1: // PRESSED — 等待热键松开（松开时才真正执行切换）
        if (!down) {
            // ---- 冷却检查 ----
            // 防止用户快速连按（200ms 内按两次）造成窗口反复切换闪烁
            {
                DWORD now = ::GetTickCount();
                if (g_Config.ToggleCooldownMs > 0 &&
                    now - s_lastToggleTime < (DWORD)g_Config.ToggleCooldownMs) {
                    s_state = 0;
                    return;  // 冷却中，丢弃此次触发
                }
                s_lastToggleTime = now;  // 在操作前重置（防止操作耗时内再次触发）
            }

            // ---- 执行切换（UE3 模式和轮询模式逻辑不同） ----
            if (g_Config.UE3Mode) {
                // UE3 模式：切换 MinHook 的拦截开关
                //   Hook 激活 → 设为不激活 → 所有 Hook 直通原始 API → 游戏恢复原生窗口
                //   Hook 不激活 → 设为激活 → Hook 继续拦截 → 重新应用无边框
                // 这种设计的好处：不需要卸载/重装 Hook（开销大），只需一个 bool 分支
                if (UE3::IsActive()) {
                    UE3::SetActive(false);
                    WindowManager::RestoreWindow(hLast);
                    Log(L"[BL] Hotkey: UE3 mode disabled (hooks bypassed)");
                } else {
                    UE3::SetActive(true);
                    WindowManager::ApplyBorderless(g_Config, hLast);
                    Log(L"[BL] Hotkey: UE3 mode enabled (hooks active)");
                }
            } else {
                // 轮询模式：直接应用/恢复窗口样式
                // g_borderlessActive 追踪当前状态 → 避免"恢复已经恢复的窗口"
                if (g_borderlessActive) {
                    WindowManager::RestoreWindow(hLast);
                    g_borderlessActive = false;
                    if (g_Config.LockCursor) WindowManager::UnlockCursor();
                    Log(L"[BL] Hotkey: borderless disabled");
                } else {
                    WindowManager::ApplyBorderless(g_Config, hLast);
                    g_borderlessActive = true;
                    if (g_Config.LockCursor) WindowManager::LockCursorToWindow(hLast);
                    Log(L"[BL] Hotkey: borderless enabled");
                }
            }
            s_state = 0;  // 回到 IDLE，等待下一次按下
        }
        break;
    }
}

// ===================================================================
// HotkeyThread — 独立热键轮询线程
//
// 【为什么独立成线程而不是放在 WorkerThread 里？】
//   这是经过实践教训后的架构修正。
//   UE3 模式不需要 WorkerThread（Hook 在 API 层拦截，轮询多余），
//   但用户仍然期望热键能工作（切换 Hook 开关）。
//   独立线程后：无论哪个模式、无论 WorkerThread 是否创建，热键始终可用。
//
// 【100ms 轮询是否浪费 CPU？】
//   Sleep(100) + 单次 GetAsyncKeyState 调用 ≈ 微秒级 CPU 时间，
//   每秒 10 次 ≈ 可以忽略不计。远低于游戏本身的 CPU 消耗。
//   PC 游戏不是移动设备，100ms 轮询对电池/续航的影响为零。
//
// 【初始 500ms Sleep 的作用】
//   给游戏引擎时间创建第一个窗口。否则 PollToggleKey 发现 g_hLastWindow
//   为 nullptr 直接 return，相当于热键在前 500ms 不工作。
//   500ms 是经验值，对大多数游戏足够（Unity 约 200ms，UE 约 400ms）。
// ===================================================================
static DWORD WINAPI HotkeyThread(LPVOID /*param*/) {
    ::Sleep(500);  // 等待游戏窗口创建
    // memory_order_acquire: 确保看到 g_bRunning=false 时，之前 DETACH 中的
    // 清理操作（UE3::Shutdown 等）都已经完成（release-acquire 配对）
    while (g_bRunning.load(std::memory_order_acquire)) {
        PollToggleKey();
        ::Sleep(100);
    }
    return 0;
}

// ===================================================================
// WorkerThread — 轮询工作线程
//
// 【仅在非 UE3 模式下运行】
//   UE3 模式用 MinHook 在 API 层实时拦截，不需要周期性"检查+修复"。
//   轮询模式则需要持续检测样式是否被引擎恢复，每 PollingIntervalMs 检查一次。
//
// 【慢速任务 vs 快速任务的分离】
//   WorkerThread 每 100ms 唤醒一次（Sleep(100)），但不是每次都做窗口维护。
//   slowTick 计数器每 100ms 递增，只在达到 slowEvery 时才执行 ApplyToMainWindow。
//   这样设计是为了在一个线程中同时支持"高频热键检测"和"低频窗口维护"。
//   不过热键检测后来拆成了独立 HotkeyThread，slowTick 机制保留了灵活性。
//
// 【单次模式 (PollingIntervalMs <= 0)】
//   找到窗口后立即退出线程。用于"只执行一次"的场景。
//   30 秒超时保护：如果一直找不到窗口，不会无限循环。
//
// 【try/catch 的作用】
//   进程内 ASI 插件崩溃 = 游戏崩溃。try/catch 确保 ApplyToMainWindow
//   中的任何异常不会导致线程退出。异常被吞掉并记录日志，线程继续运行。
//   这是防御性编程：即使有 bug，也不影响游戏本身。
// ===================================================================
static DWORD WINAPI WorkerThread(LPVOID /*param*/) {
    ::Sleep(500);  // 等待游戏引擎初始化

    Log(L"[BL] Begin polling loop (interval=%d ms, delay=%d ms)",
        g_Config.PollingIntervalMs, g_Config.DelayMs);

    DWORD startTime = ::GetTickCount();
    int  slowTick = 0;
    // slowEvery: 将 ms 级间隔转换为 100ms tick 单位
    //   PollingIntervalMs=1000 → slowEvery=10 → 每 10×100ms=1000ms 维护一次
    //   PollingIntervalMs=0    → slowEvery=0  → 被钳位到 1（每 100ms 维护）
    //   PollingIntervalMs=100  → slowEvery=1  → 每 100ms 维护
    int  slowEvery = g_Config.PollingIntervalMs / 100;
    if (slowEvery < 1) slowEvery = 1;  // 钳位：最少每 100ms 检查一次

    while (g_bRunning.load(std::memory_order_acquire)) {
        // ---- 每 slowEvery 个 tick 执行一次窗口维护 ----
        if (++slowTick >= slowEvery) {
            slowTick = 0;
            try {
                ApplyToMainWindow();
            } catch (...) {
                // 吞掉所有异常：线程不能死，死了就没人维护窗口了
                Log(L"[BL] Exception in ApplyToMainWindow");
            }

            // ---- 单次模式退出检查 ----
            // <= 0 而非 == 0：防御性编程。如果用户误配负值，走安全路径。
            if (g_Config.PollingIntervalMs <= 0) {
                if (g_hLastWindow.load(std::memory_order_relaxed) != nullptr) {
                    Log(L"[BL] One-shot mode — window applied, exiting");
                    break;  // 成功找到并处理窗口 → 退出线程
                }
                if (::GetTickCount() - startTime > 30000) {
                    Log(L"[BL] One-shot timed out — no captioned window after 30s");
                    break;  // 30 秒仍无窗口 → 放弃（游戏可能根本没有窗口）
                }
            }
        }

        ::Sleep(100);
    }

    Log(L"[BL] Worker thread exiting");
    return 0;
}

// ===================================================================
// DllMain — DLL 入口点
//
// 【ASI 加载机制】
//   Ultimate ASI Loader 在游戏进程启动时加载所有 .asi 文件。
//   它实际上是 DLL，扩展名改为 .asi 便于识别。
//   ASI Loader 调用 LoadLibrary → Windows 调用 DllMain(DLL_PROCESS_ATTACH)。
//   我们在 ATTACH 中完成所有初始化，在 DETACH 中完成所有清理。
//
// 【DllMain 中的限制（MSDN 警告）】
//   DllMain 在 loader lock 持有期间执行，不能做：
//     - 调用 LoadLibrary/FreeLibrary（可能死锁）
//     - 等待线程（WaitForSingleObject 等）— 可能死锁
//     - 调用 COM 初始化
//   我们的做法：在 ATTACH 中只创建线程（CreateThread 是允许的），
//   重型操作在线程中完成。DETACH 中等待线程退出虽然是 MSDN 不建议的，
//   但在 ASI 插件场景下是标准做法（进程即将退出，死锁风险可控）。
//
// 【为什么 DisableThreadLibraryCalls？】
//   默认情况下，每次进程创建/销毁线程，Windows 都会调用所有 DLL 的
//   DllMain(DLL_THREAD_ATTACH/DETACH)。我们的插件不需要这些通知，
//   禁用后减少不必要的函数调用开销。
// ===================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID /*lpReserved*/) {
    switch (dwReason) {
        case DLL_PROCESS_ATTACH: {
            ::DisableThreadLibraryCalls(hModule);

            // ============================================================
            // 步骤 1-2：解析路径 + 加载配置
            //
            // 【为什么配置加载必须在日志清空之前？】
            //   旧版代码先清空日志再加载配置。问题是：清空日志时 g_Config
            //   还是默认值（EnableLog=true），即使用户在 INI 中设了
            //   EnableLog=0，旧日志仍然被清空了。
            //   新版先加载配置，让用户的选择生效后再决定是否清空日志。
            // ============================================================
            std::wstring dir = GetModuleDir();
            g_LogPath = dir + L"\\BorderLimited.log";
            std::wstring iniPath = dir + L"\\BorderLimited.ini";

            g_Config = ConfigReader::Load(iniPath.c_str());

            // ---- 步骤 3：清空旧日志（尊重 EnableLog 设置） ----
            if (g_Config.EnableLog && !g_LogPath.empty()) {
                // CREATE_ALWAYS: 如果文件存在→截断为0字节；不存在→创建
                HANDLE hClear = ::CreateFileW(g_LogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hClear != INVALID_HANDLE_VALUE) ::CloseHandle(hClear);
            }

            // ---- 步骤 4：启动日志 ----
            Log(L"[BL] ========================================");
            Log(L"[BL] BorderLimited ASI loaded — %s", (sizeof(void*) == 8) ? L"x64" : L"x86");
            Log(L"[BL] Module dir: %s", dir.c_str());
            Log(L"[BL] INI path:   %s", iniPath.c_str());
            Log(L"[BL] Config loaded: Size=%d Max=%d Top=%d Menu=%d Lock=%d WinKey=%d Taskbar=%d Cursor=%d Dpi=%d Log=%d UE3=%d(%dx%d) Poll=%d Delay=%d",
                (int)g_Config.Size, g_Config.ShouldMaximize ? 1 : 0,
                g_Config.TopMost ? 1 : 0,       g_Config.RemoveMenus ? 1 : 0,
                g_Config.LockCursor ? 1 : 0,    g_Config.DisableWinKey ? 1 : 0,
                g_Config.HideWindowsTaskbar ? 1 : 0, g_Config.HideMouseCursor ? 1 : 0,
                g_Config.OverrideDpi ? 1 : 0,   g_Config.EnableLog ? 1 : 0,
                g_Config.UE3Mode ? 1 : 0,        g_Config.UE3RenderWidth, g_Config.UE3RenderHeight,
                g_Config.PollingIntervalMs,       g_Config.DelayMs);

            // ============================================================
            // 步骤 5-6：全局一次性设置（模式无关，始终执行）
            //
            // 【为什么 DPI 覆盖必须在任何窗口创建前执行？】
            //   Windows 在进程创建第一个窗口时确定该进程的 DPI 缩放策略。
            //   一旦第一个窗口创建，DPI 策略就锁定了，之后再调用无效。
            //   所以必须在 DllMain 中、游戏创建任何窗口之前调用。
            //
            // 【为什么 WinKey Hook 在 DllMain 中启动？】
            //   WH_KEYBOARD_LL 是全局钩子，与窗口无关。在 DllMain 中启动
            //   比在线程中启动更早生效，覆盖游戏启动期间的 Win 键误触。
            // ============================================================
            if (g_Config.OverrideDpi) {
                WindowManager::OverrideDpiScaling();
                Log(L"[BL] DPI scaling override applied");
            }

            if (g_Config.DisableWinKey) {
                WindowManager::StartWinKeyHook();
                Log(L"[BL] WinKey hook installed");
            }

            // ============================================================
            // 步骤 7：启动热键线程（双模式都创建）
            //
            // 【如果 ToggleHotKey=0 — 不创建 HotkeyThread？】
            //   是的。PollToggleKey 第一行就会 return，创建空转的线程没意义。
            //   节省一个线程的栈空间（Windows 默认 1MB）和 CPU 时间片。
            // ============================================================
            if (g_Config.ToggleHotKey) {
                g_hHotkeyThread = ::CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
                if (g_hHotkeyThread) {
                    Log(L"[BL] Hotkey thread created (VK=0x%02X, mod=%d)",
                        g_Config.ToggleHotKeyCode, g_Config.ToggleHotKeyMod);
                }
            }

            // ============================================================
            // 步骤 8：启动主逻辑 — 路由到轮询或 UE3 Hook
            //
            // 【UE3::Init 失败 = d3d9.dll 不可用】
            //   返回 false 时回退到 WorkerThread 轮询模式。
            //   UE3::Init 内部已调用 MH_Uninitialize() 清理残留 Hook。
            //   回退后 Hook 不残留，相当于从未尝试过 UE3 模式。
            // ============================================================
            if (g_Config.UE3Mode) {
                if (UE3::Init(g_Config)) {
                    Log(L"[BL] UE3 Mode active — D3D9 hooks installed");
                } else {
                    Log(L"[BL] ERROR: UE3::Init failed — falling back to polling");
                    g_hWorkerThread = ::CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
                    if (g_hWorkerThread) Log(L"[BL] Worker thread created (fallback)");
                }
            } else {
                g_hWorkerThread = ::CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
                if (g_hWorkerThread) {
                    Log(L"[BL] Worker thread created successfully");
                } else {
                    Log(L"[BL] ERROR: Failed to create worker thread (GetLastError=%d)",
                        ::GetLastError());
                }
            }
            break;
        }

        case DLL_PROCESS_DETACH: {
            // ============================================================
            // 清理顺序很重要！顺序错误可能导致崩溃或资源泄漏。
            //
            // 清理链（按依赖关系排序）：
            //   1. 卸载 Hook（停止拦截 API 调用）
            //   2. 通知线程退出
            //   3. 等待线程退出
            //   4. 卸载钩子（Hook 线程已退出，安全卸载）
            //   5. 恢复系统状态（光标、任务栏）
            //
            // 【为什么先 UE3::Shutdown 再通知线程退出？】
            //   UE3::Shutdown 卸载 MinHook。如果 HotkeyThread 正在执行
            //   PollToggleKey（调用 RestoreWindow → SetWindowLong），
            //   而 SetWindowLong 的 Hook 刚好被卸载 → 调用已释放的跳板函数
            //   → 崩溃。但实际上 MinHook 的 DisableHook 只是恢复原始字节，
            //   不会释放内存，所以安全。Uninitialize 在 DisableHook 之后。
            //
            // 【为什么 TerminateThread 是最后手段？】
            //   TerminateThread 不释放线程栈、不调用 TLS 析构、不释放 CRITICAL_SECTION。
            //   但 WorkerThread 可能在 Sleep(100) 中，最坏情况要等 100ms + 维护时间。
            //   3 秒超时足够覆盖任何正常情况；如果 3 秒还没退出 = 线程死锁，强杀。
            // ============================================================
            UE3::Shutdown();

            // ---- 向所有线程广播"请退出"信号 ----
            // memory_order_release: 确保之前的清理操作（Shutdown 等）对所有线程可见
            g_bRunning.store(false, std::memory_order_release);

            // ---- 等待热键线程（最多 2 秒） ----
            // 热键线程在 Sleep(100) 中，最多 100ms 内就会检查到 g_bRunning
            if (g_hHotkeyThread) {
                ::WaitForSingleObject(g_hHotkeyThread, 2000);
                ::CloseHandle(g_hHotkeyThread);
                g_hHotkeyThread = nullptr;
            }

            // ---- 等待工作线程（最多 3 秒） ----
            if (g_hWorkerThread) {
                if (::WaitForSingleObject(g_hWorkerThread, 3000) == WAIT_TIMEOUT) {
                    ::TerminateThread(g_hWorkerThread, 0);  // 3 秒超时 → 强制终止
                }
                ::CloseHandle(g_hWorkerThread);
                g_hWorkerThread = nullptr;
            }

            // ---- 卸载 WinKey 钩子 + 解除光标锁定 ----
            // 此时 Hook 线程已通过 WM_QUIT 退出（见 window.cpp StopWinKeyHook）
            WindowManager::StopWinKeyHook();
            WindowManager::UnlockCursor();

            // ---- 恢复系统状态 ----
            // 条件检查：只有之前确实隐藏了才恢复。否则会"恢复"一个没见过的东西。
            if (g_Config.HideWindowsTaskbar) {
                WindowManager::HideWindowsTaskbar(false);
            }
            if (g_Config.HideMouseCursor) {
                WindowManager::HideMouseCursor(false);
            }
            break;
        }
    }
    return TRUE;
}
