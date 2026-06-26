// ===================================================================
// config.cpp — 轻量级 INI 配置文件解析器实现
//
// 【设计决策 — 为什么不用 Windows 的 GetPrivateProfileString？】
//   GetPrivateProfileString 是 Win32 的 INI 读取 API，但有严重限制：
//     1. 路径限制：在 Win9x 时代要求文件在 Windows 目录下
//     2. 性能问题：每次调用都重新打开/解析整个文件
//     3. 缓存行为不可控：内部有静默缓存，配置热更新不可靠
//     4. 编码支持差：不处理 UTF-8 BOM，非 ANSI 字符可能乱码
//   自实现解析器一次性读入 → 内存中查找 → O(1) 读取，无路径限制。
//
// 【编码策略 — UTF-8 优先，ANSI 回退】
//   现代编辑器（VS Code, Notepad++, Sublime）默认 UTF-8。
//   Windows 记事本（特别是中文系统）可能保存为 ANSI (CP_ACP = GBK)。
//   尝试顺序: UTF-8 → ANSI
//   MB_ERR_INVALID_CHARS 标志让 UTF-8 尝试快速失败（遇到非 UTF-8 直接返回 0,
//   不像默认行为那样用 ? 替换非法序列）。
//
// 【为什么不支持 UTF-16？】
//   UTF-16 LE 有特殊的 BOM (FF FE)，可以检测。但 ASI 插件的日志系统
//   使用 UTF-8，Config GUI 也用 UTF-8 保存。统一 UTF-8 避免多编码混乱。
//   如果用户用记事本选"Unicode"保存(UTF-16 LE)，编码转换失败 →
//   OutputDebugString 提示 → 返回默认配置。
//
// 【MultiByteToWideChar 的正确调用】
//   第一次调用：传 nullptr + 0 作为输出缓冲，函数返回所需宽字符数 (len)
//   第二次调用：传入大小为 len 的缓冲，函数写入 len 个宽字符
//   关键：传入显式 cbMultiByte 时，len 不含 null 终止符！
//   resize(len) 而非 resize(len-1) — 这是实际验证过的正确用法。
//   resize 后的 wstring 内部 buffer 有 len+1 个元素（+1 为 null），刚好容纳。
// ===================================================================

#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace {

    // 剥离 ';' 内联注释："FullScreen ; 全屏模式" → "FullScreen "
    std::wstring StripComment(const std::wstring& s) {
        auto pos = s.find(L';');
        if (pos == std::wstring::npos) return s;
        return s.substr(0, pos);
    }

    // 去除首尾空白（空格和制表符）
    std::wstring Trim(const std::wstring& s) {
        size_t start = 0;
        while (start < s.size() && (s[start] == L' ' || s[start] == L'\t'))
            ++start;
        size_t end = s.size();
        while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'\t'))
            --end;
        return s.substr(start, end - start);
    }

    // 清理值：剥离注释 + 去空白 + 去双引号包裹
    std::wstring CleanValue(const std::wstring& raw) {
        std::wstring v = Trim(StripComment(raw));
        if (v.size() >= 2 && v.front() == L'"' && v.back() == L'"')
            v = v.substr(1, v.size() - 2);
        return v;
    }

} // anonymous namespace

// ===================================================================
// ReadValue — 从 [Default] 段中定位并读取键值
//
// 【段边界检测 — 为什么用 "\n[" 而不是 "["？】
//   "[" 会匹配注释中的方括号，例如：
//     "[Default]\nSize=FullScreen ; [这是注释不是段]"
//   用 "\n[" 确保只匹配行首的方括号（真正的段头）。
//   这里假设文件以 \n 结尾（大多数文本文件如此），如果文件末尾
//   无换行且键值在最后一行，endPos 会等于 text.size()（正确）。
// ===================================================================
std::wstring ConfigReader::ReadValue(const wchar_t* buf, const wchar_t* key, const wchar_t* defaultVal) {
    if (!buf || !key) return std::wstring(defaultVal);

    std::wstring text(buf);

    // 1) 定位 [Default] 段头
    auto pos = text.find(L"[Default]");
    if (pos == std::wstring::npos) return std::wstring(defaultVal);

    // 2) 找到段结束位置（下一个 "\n[" 或文件尾）
    auto sectionStart = pos + 9;  // len("[Default]") = 9
    auto endPos = text.find(L"\n[", sectionStart);
    if (endPos != std::wstring::npos) endPos = endPos + 1;  // 指向 '\n[' 的 '['
    else endPos = text.size();

    // 3) 在段内搜索 "key="
    std::wstring needle = std::wstring(key) + L"=";
    auto keyPos = text.find(needle, sectionStart);
    if (keyPos == std::wstring::npos || keyPos >= endPos) return std::wstring(defaultVal);

    // 4) 提取值（从 '=' 后到行尾）
    keyPos += needle.size();
    auto lineEnd = text.find_first_of(L"\r\n", keyPos);
    if (lineEnd == std::wstring::npos) lineEnd = text.size();

    std::wstring raw = text.substr(keyPos, lineEnd - keyPos);
    return CleanValue(raw);
}

// 读取整数值 — 解析失败时静默回退默认值（用户输入 "abc" 不会崩溃）
int ConfigReader::ReadInt(const wchar_t* buf, const wchar_t* key, int defaultVal) {
    std::wstring val = ReadValue(buf, key, L"");
    if (val.empty()) return defaultVal;
    try {
        return std::stoi(val);  // 可抛 std::invalid_argument / std::out_of_range
    } catch (...) {
        return defaultVal;
    }
}

// 读取布尔值 — 接受多种真值格式
bool ConfigReader::ReadBool(const wchar_t* buf, const wchar_t* key, bool defaultVal) {
    std::wstring val = ReadValue(buf, key, L"");
    if (val.empty()) return defaultVal;
    std::transform(val.begin(), val.end(), val.begin(), ::towlower);
    return (val == L"1" || val == L"true" || val == L"yes" || val == L"on");
}

// 读取窗口尺寸枚举值
WindowSize ConfigReader::ReadSizeEnum(const wchar_t* buf, const wchar_t* key, WindowSize defaultVal) {
    std::wstring val = ReadValue(buf, key, L"");
    if (val.empty()) return defaultVal;
    std::transform(val.begin(), val.end(), val.begin(), ::towlower);
    if (val == L"fullscreen") return WindowSize::FullScreen;
    if (val == L"custom")     return WindowSize::Custom;
    if (val == L"borderless")   return WindowSize::Borderless;
    return defaultVal;
}

// ===================================================================
// Load — 加载 INI 文件的主入口
//
// 【零配置可用策略】
//   INI 不存在 → 自动生成一份完整的默认配置 INI → 返回默认值
//   用户后续编辑这个生成的 INI 即可，不用手写格式。
//
// 【为什么默认配置用 ANSI 文本而不是 UTF-8？】
//   kDefaultIni 只含纯 ASCII 字符（无中文/特殊字符），ANSI = UTF-8 = ASCII
//   子集。任何编码环境都能正确读取。Config GUI 保存时才写 UTF-8。
//   自动生成的 INI 全 ASCII，保证最大兼容性。
// ===================================================================
AppConfig ConfigReader::Load(const wchar_t* iniPath) {
    AppConfig cfg;  // 所有字段初始化为 C++ 默认值

    // ---- 读文件 ----
    std::ifstream file(iniPath, std::ios::binary);
    if (!file.is_open()) {
        // 首次启动 — 生成默认 INI
        // kDefaultIni 是纯 ASCII（= UTF-8 子集），跨编码安全
        static const char* kDefaultIni =
            "[Default]\r\n"
            "Size=FullScreen\r\n"
            "PositionX=0\r\nPositionY=0\r\nPositionW=0\r\nPositionH=0\r\n"
            "OffsetL=0\r\nOffsetT=0\r\nOffsetR=0\r\nOffsetB=0\r\n"
            "ShouldMaximize=1\r\nTopMost=0\r\nRemoveMenus=0\r\n"
            "LockCursor=0\r\nDisableWinKey=0\r\n"
            "HideWindowsTaskbar=0\r\nHideMouseCursor=0\r\n"
            "OverrideDpi=0\r\nEnableLog=1\r\n"
            "UE3Mode=0\r\nUE3RenderWidth=0\r\nUE3RenderHeight=0\r\n"
            "PollingIntervalMs=1000\r\nDelayMs=0\r\n"
            "ToggleHotKey=1\r\nToggleHotKeyCode=121\r\nToggleHotKeyMod=1\r\n"
            "ToggleCooldownMs=2000\r\nSendExitSizeMove=0\r\n";
        std::ofstream out(iniPath, std::ios::binary | std::ios::trunc);
        if (out.is_open()) {
            out.write(kDefaultIni, strlen(kDefaultIni));
            out.close();
        }
        return cfg;
    }

    // ---- 一次读入整个文件到内存 ----
    std::string rawBytes((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    file.close();
    if (rawBytes.empty()) return cfg;

    // ---- 跳过 UTF-8 BOM (EF BB BF) ----
    // BOM 是文件开头的 3 字节标记，表示"此文件是 UTF-8 编码"。
    // 很多编辑器自动加 BOM，但 BOM 不是有效的内容字符，
    // 如果不去掉会导致 [Default] 前面多出 3 个不可见字符从而找不到段头
    if (rawBytes.size() >= 3 &&
        (unsigned char)rawBytes[0] == 0xEF &&
        (unsigned char)rawBytes[1] == 0xBB &&
        (unsigned char)rawBytes[2] == 0xBF) {
        rawBytes.erase(0, 3);
    }

    // ---- 窄字符串 → 宽字符串（UTF-8 优先，ANSI 回退） ----
    std::wstring content;
    for (UINT codePage : {CP_UTF8, CP_ACP}) {
        int len = ::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                                        rawBytes.c_str(), (int)rawBytes.size(),
                                        nullptr, 0);
        if (len > 0) {
            // len = 不含 null 的精确宽字符数（cbMultiByte ≠ -1）
            content.resize(len);       // 非 len-1！
            ::MultiByteToWideChar(codePage, 0,
                                  rawBytes.c_str(), (int)rawBytes.size(),
                                  &content[0], len);
            break;
        }
    }

    if (content.empty()) {
        // 两种编码都失败（可能是 UTF-16 或其他不支持的编码）
        // OutputDebugString 提示开发者排查，返回默认配置保证插件仍可工作
        ::OutputDebugStringW(L"[BorderLimited] INI encoding error — unsupported format, using defaults\r\n");
        return cfg;
    }

    const wchar_t* buf = content.c_str();

    // ================================================================
    // 解析全部 28 个配置字段
    //
    // 【字段分组】
    //   A 类 (窗口尺寸)   : Size + Position*4 + Offset*4 = 9
    //   B 类 (窗口行为)   : Maximize/TopMost/Menus/LockCursor/DisableWinKey/
    //                       HideTaskbar/HideCursor/OverrideDpi/EnableLog = 9
    //   C 类 (引擎适配)   : UE3Mode + UE3Render*2 = 3
    //   D 类 (高级/热键)  : Poll/Delay + ToggleHotKey/Code/Mod/Cooldown + SendExitSizeMove = 7
    //   合计 28 字段
    // ================================================================

    // (A) 窗口尺寸与位置
    cfg.Size               = ReadSizeEnum(buf, L"Size", WindowSize::FullScreen);
    cfg.PositionX          = ReadInt(buf, L"PositionX", 0);
    cfg.PositionY          = ReadInt(buf, L"PositionY", 0);
    cfg.PositionW          = ReadInt(buf, L"PositionW", 0);
    cfg.PositionH          = ReadInt(buf, L"PositionH", 0);
    cfg.OffsetL            = ReadInt(buf, L"OffsetL", 0);
    cfg.OffsetT            = ReadInt(buf, L"OffsetT", 0);
    cfg.OffsetR            = ReadInt(buf, L"OffsetR", 0);
    cfg.OffsetB            = ReadInt(buf, L"OffsetB", 0);

    // (B) 窗口行为选项
    cfg.ShouldMaximize     = ReadBool(buf, L"ShouldMaximize", true);
    cfg.TopMost            = ReadBool(buf, L"TopMost", false);
    cfg.RemoveMenus        = ReadBool(buf, L"RemoveMenus", false);
    cfg.HideWindowsTaskbar = ReadBool(buf, L"HideWindowsTaskbar", false);
    cfg.HideMouseCursor    = ReadBool(buf, L"HideMouseCursor", false);
    cfg.LockCursor         = ReadBool(buf, L"LockCursor", false);
    cfg.DisableWinKey      = ReadBool(buf, L"DisableWinKey", false);
    cfg.OverrideDpi        = ReadBool(buf, L"OverrideDpi", false);
    cfg.EnableLog          = ReadBool(buf, L"EnableLog", true);

    // (C) 引擎适配
    cfg.UE3Mode            = ReadBool(buf, L"UE3Mode", false);
    cfg.UE3RenderWidth     = ReadInt(buf, L"UE3RenderWidth", 0);
    cfg.UE3RenderHeight    = ReadInt(buf, L"UE3RenderHeight", 0);

    // (D) 高级参数 + 热键
    cfg.PollingIntervalMs  = ReadInt(buf, L"PollingIntervalMs", 1000);
    cfg.DelayMs            = ReadInt(buf, L"DelayMs", 0);
    cfg.ToggleHotKey       = ReadBool(buf, L"ToggleHotKey", true);
    cfg.ToggleHotKeyCode   = ReadInt(buf, L"ToggleHotKeyCode", VK_F10);
    cfg.ToggleHotKeyMod    = ReadInt(buf, L"ToggleHotKeyMod", MOD_ALT);
    cfg.ToggleCooldownMs   = ReadInt(buf, L"ToggleCooldownMs", 2000);
    cfg.SendExitSizeMove   = ReadBool(buf, L"SendExitSizeMove", false);

    return cfg;
}
