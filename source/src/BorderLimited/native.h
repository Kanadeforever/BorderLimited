// ===================================================================
// native.h — Win32 API 辅助函数、窗口样式常量、窗口查找工具
//
// 【本文件的角色】
//   这是最底层的工具库。所有窗口操作（ApplyBorderless, FindWindow 等）
//   都依赖这里的函数和常量。不依赖任何其他项目文件。
//
// 【依赖设计】
//   仅依赖 windows.h + string — 无第三方库。
//   全 inline 实现（因为是 header-only，减少编译单元复杂度）。
//   对于只有 2 个 .cpp 的 ASI 项目，inline 没有代码膨胀问题。
//
// 【窗口样式位掩码的设计】
//   每个窗口有两个 32 位的样式字段：
//     GWL_STYLE    — 标准样式（边框/标题栏/系统菜单等）
//     GWL_EXSTYLE  — 扩展样式（3D边缘/工具窗口标记等）
//
//   去无边框的原理：读当前值 → 位运算清除目标位 → 写回。
//   哪些位需要清除由这里的 StylesToRemove / ExtendedStylesToRemove 定义。
//
//   【重要：这些常量是轮询路径和 UE3 Hook 路径的共享定义】
//   两边都引用同一套常量，保证位掩码一致性。
//   如果将来需要增删样式位，只需改这里 — 两条路径自动同步。
// ===================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // 裁剪 windows.h（排除不常用的 API 声明，加速编译）
#endif

#include <windows.h>
#include <tlhelp32.h>   // CreateToolhelp32Snapshot — 遍历进程线程
#include <string>

// ===================================================================
// WindowStyle — 窗口样式位掩码常量
// ===================================================================
namespace WindowStyle {

    // ---- 标准窗口样式 (GWL_STYLE) ----
    // 这些位控制窗口的基本外观。
    // 每一个常量是一个 32 位整数，其中恰好一个二进制位是 1。
    // | 运算符把它们组合成一个"要清除的位集合"。
    constexpr LONG StylesToRemove =
        WS_CAPTION        // 0x00C00000 — 标题栏 + 细边框的复合位
        | WS_BORDER       // 0x00800000 — 细线边框（不可调整大小的窗口）
        | WS_DLGFRAME     // 0x00400000 — 对话框风格边框（浮雕/凹陷效果）
        | WS_THICKFRAME   // 0x00040000 — 粗边框（可拖拽调整窗口大小）
        | WS_SYSMENU      // 0x00080000 — 系统菜单（左上角图标+右键标题栏菜单）
        | WS_MAXIMIZEBOX  // 0x00010000 — 最大化按钮
        | WS_MINIMIZEBOX; // 0x00020000 — 最小化按钮
    // 注：WS_OVERLAPPEDWINDOW = 上述所有位的合集（0x00CF0000），
    // 清除上述位后该复合位自然清零，无需显式加入。

    // ---- 扩展窗口样式 (GWL_EXSTYLE) ----
    constexpr LONG ExtendedStylesToRemove =
        WS_EX_DLGMODALFRAME  // 0x00000001 — 对话框模态边框（凹陷 3D 效果）
        | WS_EX_WINDOWEDGE   // 0x00000100 — 凸起边缘边框
        | WS_EX_CLIENTEDGE   // 0x00000200 — 客户区凹陷边缘
        | WS_EX_STATICEDGE   // 0x00020000 — 静态控件的 3D 边框（不用于主窗口但无害）
        | WS_EX_TOOLWINDOW   // 0x00000080 — 工具窗口标记（不显示在 Alt+Tab 中）
        | WS_EX_APPWINDOW;   // 0x00040000 — 强制显示在任务栏（覆盖 TOOLWINDOW）
}

// ===================================================================
// NativeUtil — 窗口属性查询和进程主窗口查找
// ===================================================================
namespace NativeUtil {

    // ---- 窗口类名查询 ----
    // 类名是标识窗口"类型"的字符串（如 "UnityWndClass", "LaunchUnrealUWindowsClient"）
    // buffer[256]: 类名最长 256 字符（Windows 限制）
    inline std::wstring GetWindowClassName(HWND hwnd) {
        wchar_t buffer[256] = {};
        ::GetClassNameW(hwnd, buffer, 256);
        return std::wstring(buffer);
    }

    // ---- 窗口标题查询 ----
    // 用 WM_GETTEXT 而非 GetWindowTextW：先查长度再分配缓冲，避免截断
    inline std::wstring GetWindowTitle(HWND hwnd) {
        int length = (int)::SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0);
        if (length <= 0) return L"";
        std::wstring result;
        result.resize(length + 1);  // +1 留给 null 终止符
        int copied = (int)::SendMessageW(hwnd, WM_GETTEXT, (WPARAM)(length + 1), (LPARAM)&result[0]);
        if (copied > 0) result.resize(copied);
        else result.clear();
        return result;
    }

    // ---- 无边框状态检测 ----
    // 读当前样式 → 与要去除的位做 & → 如果要去除的位全部为 0 → 已无边框
    inline bool IsAlreadyBorderless(HWND hwnd) {
        LONG style   = (LONG)::GetWindowLongPtrW(hwnd, GWL_STYLE);
        LONG exStyle = (LONG)::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        bool noStandard = (style   & WindowStyle::StylesToRemove)         == 0;
        bool noExtended = (exStyle & WindowStyle::ExtendedStylesToRemove) == 0;
        return noStandard && noExtended;
    }

    // ================================================================
    // 窗口查找 — 多策略回退
    //
    // 【为什么需要两种策略？】
    //   EnumWindows（策略 1）: 遍历系统所有顶层窗口 → 筛选本进程的。
    //     适用于 99% 的游戏。
    //   EnumThreadWindows（策略 2）: 遍历本进程每个线程的顶层窗口列表。
    //     某些游戏（特别是使用嵌入式浏览器/多进程渲染的）
    //     主窗口可能不是常规"顶层窗口"，EnumWindows 找不到。
    //     CreateToolhelp32Snapshot 获取本进程的线程列表 → 逐个
    //     EnumThreadWindows → 找到第一个满足条件的窗口。
    //     性能开销较高（遍历线程+枚举），只在策略 1 失败时回退。
    //
    // 【为什么要筛选 WS_CAPTION？】
    //   游戏进程可能有多个窗口：消息窗口、D3D 设备窗口、IME 窗口等。
    //   这些辅助窗口通常没有 WS_CAPTION 样式。
    //   筛选 WS_CAPTION 能有效排除非主窗口。
    //   如果游戏主窗口也没有 WS_CAPTION（极少见），两种策略都返回 nullptr。
    // ================================================================

    struct FindWindowParam { HWND h; DWORD pid; };

    // 策略 1 的回调
    inline BOOL CALLBACK FindWindowCallback(HWND hwnd, LPARAM lParam) {
        FindWindowParam* p = (FindWindowParam*)lParam;
        DWORD windowPid = 0;
        ::GetWindowThreadProcessId(hwnd, &windowPid);
        if (windowPid != p->pid) return TRUE;     // 不是本进程的窗口 → 跳过
        if (!::IsWindowVisible(hwnd)) return TRUE; // 不可见（隐藏窗口）→ 跳过
        LONG style = (LONG)::GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (!(style & WS_CAPTION)) return TRUE;    // 无标题栏 → 跳过
        p->h = hwnd;
        return FALSE;  // 找到！停止枚举
    }

    inline HWND FindViaEnumWindows() {
        FindWindowParam param = { nullptr, ::GetCurrentProcessId() };
        ::EnumWindows(FindWindowCallback, (LPARAM)&param);
        return param.h;
    }

    // 策略 2 的回调（EnumThreadWindows 不需要检查 pid — 已限定线程）
    inline BOOL CALLBACK ThreadWindowCallback(HWND hwnd, LPARAM lParam) {
        FindWindowParam* p = (FindWindowParam*)lParam;
        if (!::IsWindowVisible(hwnd)) return TRUE;
        LONG style = (LONG)::GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (!(style & WS_CAPTION)) return TRUE;
        p->h = hwnd;
        return FALSE;
    }

    inline HWND FindViaEnumThreadWindows() {
        DWORD pid = ::GetCurrentProcessId();
        // 创建进程内所有线程的快照
        HANDLE hSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return nullptr;

        FindWindowParam param = { nullptr, pid };
        THREADENTRY32 te = { sizeof(te) };  // dwSize 必须初始化
        if (::Thread32First(hSnap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid) continue;  // 只处理本进程的线程
                // 打开线程以查询其窗口（THREAD_QUERY_INFORMATION 权限足够）
                HANDLE hThread = ::OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (hThread) {
                    ::EnumThreadWindows(te.th32ThreadID, ThreadWindowCallback, (LPARAM)&param);
                    ::CloseHandle(hThread);
                    if (param.h != nullptr) break;  // 找到了 → 退出循环
                }
            } while (::Thread32Next(hSnap, &te));
        }
        ::CloseHandle(hSnap);
        return param.h;
    }

    // 主入口：策略 1 优先 → 策略 2 回退
    inline HWND FindProcessMainWindow() {
        HWND hwnd = FindViaEnumWindows();
        if (hwnd) return hwnd;
        return FindViaEnumThreadWindows();
    }
}
