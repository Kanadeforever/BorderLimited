#pragma once

// ===================================================================
// config.h — INI 配置文件解析器
//
// BorderLimited ASI 插件的所有行为由一个 INI 文件控制
// (BorderLimited.ini, 放在 ASI 插件同目录下)。
// 本文件定义配置数据的结构 AppConfig 和解析类 ConfigReader。
//
// 设计原则：
//   1. 不依赖 Windows GetPrivateProfileString — 那个 API 要求文件在
//      Windows 目录或注册表路径下。我们使用自定义的轻量解析器。
//   2. 零配置可用 — 如果 INI 不存在，自动生成一份默认配置。
//   3. 单段设计 — 整个 INI 只有一个 [Default] 段。ASI 在游戏进程内
//      运行，不需要多段（每个游戏单独放 ASI + INI）。
//   4. 向前兼容 — 新增配置项有合理的默认值，旧版 INI 也能正常工作。
//
// 配置项共 28 个，分为五大类：
//   (A) 窗口尺寸/位置 (Size, Position*, Offset*)          — 9 个
//   (B) 窗口行为 (ShouldMaximize, TopMost, RemoveMenus, ...) — 9 个
//   (C) 引擎适配 (UE3Mode, UE3RenderWidth/Height)          — 3 个
//   (D) 热键切换 (ToggleHotKey, Code, Mod, Cooldown)       — 4 个
//   (E) 高级/时序 (PollingIntervalMs, DelayMs, SendExitSizeMove) — 3 个
// 合计 28 字段
// ===================================================================

#include <windows.h>
#include <string>

// ------------------------------------------------------------------
// 窗口尺寸模式枚举
// ------------------------------------------------------------------
enum class WindowSize {
    Borderless   = 0,  // 仅移除边框和标题栏，不改变窗口位置和大小
    FullScreen = 1,  // 铺满窗口所在的显示器（默认模式）
    Custom     = 2   // 使用 PositionX/Y/W/H 指定的精确坐标
};

// ------------------------------------------------------------------
// AppConfig — 完整的运行时配置数据结构
//
// 所有字段都有 C++ 默认值，对应无 INI 文件时的"零配置"行为。
// 这个结构体在 DllMain 中加载一次，全局只读使用。
// ------------------------------------------------------------------
struct AppConfig {
    // ============ A 类：窗口尺寸与位置 ============

    // 窗口尺寸策略（全屏 / 自定义 / 无边框窗口）
    WindowSize Size = WindowSize::FullScreen;

    // 自定义窗口位置（仅当 Size == Custom 时生效）
    // 坐标系：虚拟屏幕坐标 (SM_XVIRTUALSCREEN / SM_YVIRTUALSCREEN)
    int PositionX = 0;   // 窗口左上角 X 坐标
    int PositionY = 0;   // 窗口左上角 Y 坐标
    int PositionW = 0;   // 窗口宽度（像素）
    int PositionH = 0;   // 窗口高度（像素）

    // 四边偏移量，从目标矩形向内侧收缩（正值）或向外扩展（负值）
    // 用于微调窗口覆盖范围，例如隐藏任务栏残留的 1px 边缘
    int OffsetL = 0;   // 左侧偏移（Left）
    int OffsetT = 0;   // 顶部偏移（Top）
    int OffsetR = 0;   // 右侧偏移（Right）
    int OffsetB = 0;   // 底部偏移（Bottom）

    // ============ B 类：窗口行为选项 ============

    bool ShouldMaximize      = true;   // 去边框后调用 ShowWindow(SW_MAXIMIZE)
    bool TopMost             = false;  // 设置 HWND_TOPMOST 使窗口始终在最顶层
    bool RemoveMenus         = false;  // 移除 Win32 原生菜单栏（不可逆操作！）
    bool LockCursor          = false;  // 用 ClipCursor 将鼠标锁定在窗口范围内
    bool DisableWinKey       = false;  // 通过 WH_KEYBOARD_LL 钩子拦截左右 Win 键
    bool HideWindowsTaskbar  = false;  // 隐藏 Windows 任务栏 (Shell_TrayWnd) + 扩展工作区
    bool HideMouseCursor     = false;  // 将系统光标替换为完全透明的空白光标
    bool OverrideDpi         = false;  // 禁止 Windows DPI 虚拟化缩放（老游戏在 4K 屏不模糊）
    bool EnableLog           = true;   // 写入 BorderLimited.log 日志文件（UTF-8, 追加模式）

    // ============ C 类：引擎适配 ============

    bool UE3Mode             = false;  // 启用 D3D9 API Hook 模式（用于虚幻引擎 3 游戏）

    // UE3 模式下的伪造渲染分辨率（0 = 使用桌面分辨率自动检测）
    int  UE3RenderWidth      = 0;
    int  UE3RenderHeight     = 0;

    // ============ D 类：高级/时序参数 ============

    // 窗口检测轮询间隔（毫秒）
    // 设为 0 表示"仅执行一次"模式：找到窗口后立即应用配置，然后退出工作线程
    int PollingIntervalMs = 1000;

    // 样式设置延迟（毫秒）
    // 有些游戏引擎（如 GameMaker）在创建窗口后需要短暂延迟才能正确应用样式
    // UE3 模式下此设置无效（因为走 Hook 路径而非轮询）
    int DelayMs = 0;

    // ============ 热键切换 ============

    // 是否启用无边框切换热键
    bool ToggleHotKey    = true;
    // 热键虚拟键码 (默认 VK_F10 = 121)
    int  ToggleHotKeyCode = VK_F10;
    // 热键修饰键掩码: 1=Alt, 2=Ctrl, 4=Shift, 8=Win
    // 默认 1 (MOD_ALT), 即 Alt+F10
    int  ToggleHotKeyMod  = MOD_ALT;

    // 热键冷却间隔（毫秒），防止快速连按造成窗口反复切换闪烁
    // 设为 0 禁用冷却，默认 2000ms
    int  ToggleCooldownMs = 2000;

    // 调整窗口后发送 WM_EXITSIZEMOVE，欺骗游戏引擎用户已完成调整
    // 部分老引擎在收到此消息前不会更新渲染缓冲区（hotsampling 场景有用）
    bool SendExitSizeMove = false;
};


// ------------------------------------------------------------------
// ConfigReader — 极简 INI 解析器
//
// 与 Windows 自带的 GetPrivateProfileString 相比：
//   优点：不限制文件路径（可读 ASI 同目录的 INI）
//   缺点：仅支持单段 [Default]，不支持多段
//   编码：自动检测 UTF-8 优先，失败则回退到系统 ANSI 代码页
// ------------------------------------------------------------------
class ConfigReader {
public:
    // 从指定路径加载 INI 文件
    // 如果文件不存在，自动生成默认 INI 文件，然后返回默认配置
    static AppConfig Load(const wchar_t* iniPath);

private:
    // 从已解析的文本缓冲区中读取字符串值
    static std::wstring ReadValue(const wchar_t* buf, const wchar_t* key, const wchar_t* defaultVal);

    // 辅助解析方法：整数 / 布尔 / 枚举
    static int ReadInt(const wchar_t* buf, const wchar_t* key, int defaultVal);
    static bool ReadBool(const wchar_t* buf, const wchar_t* key, bool defaultVal);
    static WindowSize ReadSizeEnum(const wchar_t* buf, const wchar_t* key, WindowSize defaultVal);
};
