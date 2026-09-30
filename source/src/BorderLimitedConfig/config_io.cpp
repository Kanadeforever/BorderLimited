#include "config_io.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>

// ===================================================================
// config_io.cpp — INI 文件读写实现
//
// 解析器复用 ASI 插件相同的逻辑（单段 [Default]、UTF-8 优先、BOM 跳过、
// 内联注释剥离、值清理）。注意这里的解析逻辑需要和 ASI 的 config.cpp
// 保持同步，确保两个程序读写的 INI 格式完全兼容。
// ===================================================================

// 去除首尾空白（空格和制表符）
static std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t')) ++b;
    while (e > b && (s[e-1] == L' ' || s[e-1] == L'\t')) --e;
    return s.substr(b, e - b);
}

// 剥离 ';' 内联注释
static std::wstring StripComment(const std::wstring& s) {
    auto p = s.find(L';');
    return (p == std::wstring::npos) ? s : s.substr(0, p);
}

// 从 [Default] 段中读取指定键的值
static std::wstring ReadVal(const std::wstring& text, const wchar_t* key) {
    // 定位 [Default] 段头
    auto pos = text.find(L"[Default]");
    if (pos == std::wstring::npos) return L"";
    size_t sec = pos + 9;  // len("[Default]")

    // 找到段结束位置（下一个 "\n[" 或文件尾）
    auto nxt = text.find(L"\n[", sec);
    size_t end = (nxt != std::wstring::npos) ? nxt + 1 : text.size();

    // 在段内搜索 "key="
    std::wstring needle = std::wstring(key) + L"=";
    auto kp = text.find(needle, sec);
    if (kp == std::wstring::npos || kp >= end) return L"";

    // 提取值：'=' 之后到行尾
    kp += needle.size();
    auto le = text.find_first_of(L"\r\n", kp);
    if (le == std::wstring::npos) le = text.size();
    return Trim(StripComment(text.substr(kp, le - kp)));
}

// 类型转换辅助
static int ReadInt(const std::wstring& t, const wchar_t* k, int def) {
    auto v = ReadVal(t, k); if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}
static bool ReadBool(const std::wstring& t, const wchar_t* k, bool def) {
    auto v = ReadVal(t, k); if (v.empty()) return def;
    std::transform(v.begin(), v.end(), v.begin(), ::towlower);
    return (v == L"1" || v == L"true" || v == L"yes" || v == L"on");
}
static WindowSize ReadSize(const std::wstring& t, const wchar_t* k, WindowSize def) {
    auto v = ReadVal(t, k); if (v.empty()) return def;
    std::transform(v.begin(), v.end(), v.begin(), ::towlower);
    if (v == L"fullscreen") return WindowSize::FullScreen;
    if (v == L"custom")     return WindowSize::Custom;
    if (v == L"borderless")   return WindowSize::Borderless;
    return def;
}

bool ConfigLoad(const wchar_t* path, ConfigData& cfg) {
    // 读取整个文件
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (raw.empty()) return false;

    // 跳过 BOM
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        raw.erase(0, 3);

    // 转为宽字符串（UTF-8 优先）
    std::wstring text;
    for (UINT cp : {CP_UTF8, CP_ACP}) {
        int len = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, raw.c_str(), (int)raw.size(), nullptr, 0);
        if (len > 0) { text.resize(len); MultiByteToWideChar(cp, 0, raw.c_str(), (int)raw.size(), &text[0], len); break; }
    }
    if (text.empty()) return false;

    // 解析全部 26 个字段
    cfg.Size               = ReadSize(text, L"Size", WindowSize::FullScreen);
    cfg.PositionX          = ReadInt (text, L"PositionX", 0);
    cfg.PositionY          = ReadInt (text, L"PositionY", 0);
    cfg.PositionW          = ReadInt (text, L"PositionW", 0);
    cfg.PositionH          = ReadInt (text, L"PositionH", 0);
    cfg.OffsetL            = ReadInt (text, L"OffsetL", 0);
    cfg.OffsetT            = ReadInt (text, L"OffsetT", 0);
    cfg.OffsetR            = ReadInt (text, L"OffsetR", 0);
    cfg.OffsetB            = ReadInt (text, L"OffsetB", 0);
    cfg.ShouldMaximize     = ReadBool(text, L"ShouldMaximize", true);
    cfg.TopMost            = ReadBool(text, L"TopMost", false);
    cfg.RemoveMenus        = ReadBool(text, L"RemoveMenus", false);
    cfg.LockCursor         = ReadBool(text, L"LockCursor", false);
    cfg.DisableWinKey      = ReadBool(text, L"DisableWinKey", false);
    cfg.HideWindowsTaskbar  = ReadBool(text, L"HideWindowsTaskbar", false);
    cfg.HideMouseCursor    = ReadBool(text, L"HideMouseCursor", false);
    cfg.OverrideDpi        = ReadBool(text, L"OverrideDpi", false);
    cfg.EnableLog          = ReadBool(text, L"EnableLog", true);
    cfg.UE3Mode            = ReadBool(text, L"UE3Mode", false);
    cfg.PollingIntervalMs  = ReadInt (text, L"PollingIntervalMs", 1000);
    cfg.DelayMs            = ReadInt (text, L"DelayMs", 0);
    cfg.UE3RenderWidth     = ReadInt (text, L"UE3RenderWidth", 0);
    cfg.UE3RenderHeight    = ReadInt (text, L"UE3RenderHeight", 0);
    cfg.ToggleHotKey       = ReadBool(text, L"ToggleHotKey",       true);
    cfg.ToggleHotKeyCode   = ReadInt (text, L"ToggleHotKeyCode",   VK_F10);
    cfg.ToggleHotKeyMod    = ReadInt (text, L"ToggleHotKeyMod",    MOD_ALT);
    cfg.ToggleCooldownMs   = ReadInt (text, L"ToggleCooldownMs",   2000);
    cfg.SendExitSizeMove   = ReadBool(text, L"SendExitSizeMove",   false);
    return true;
}

bool ConfigSave(const wchar_t* path, const ConfigData& cfg) {
    // 编码 Size 枚举为字符串
    const wchar_t* sizeStr = L"FullScreen";
    if (cfg.Size == WindowSize::Custom)   sizeStr = L"Custom";
    if (cfg.Size == WindowSize::Borderless) sizeStr = L"Borderless";

    // 格式化输出（纯 ASCII/UNICODE，无注释）与 ASI 生成的 INI 格式一致
    wchar_t buf[2048];
    swprintf_s(buf, 2048,
        L"[Default]\r\n"
        L"Size=%s\r\n"
        L"PositionX=%d\r\nPositionY=%d\r\nPositionW=%d\r\nPositionH=%d\r\n"
        L"OffsetL=%d\r\nOffsetT=%d\r\nOffsetR=%d\r\nOffsetB=%d\r\n"
        L"ShouldMaximize=%d\r\nTopMost=%d\r\nRemoveMenus=%d\r\n"
        L"LockCursor=%d\r\nDisableWinKey=%d\r\n"
        L"HideWindowsTaskbar=%d\r\nHideMouseCursor=%d\r\n"
        L"OverrideDpi=%d\r\nEnableLog=%d\r\n"
        L"UE3Mode=%d\r\nUE3RenderWidth=%d\r\nUE3RenderHeight=%d\r\n"
        L"ToggleHotKey=%d\r\nToggleHotKeyCode=%d\r\nToggleHotKeyMod=%d\r\n"
        L"ToggleCooldownMs=%d\r\nSendExitSizeMove=%d\r\n"
        L"PollingIntervalMs=%d\r\nDelayMs=%d\r\n",
        sizeStr,
        cfg.PositionX, cfg.PositionY, cfg.PositionW, cfg.PositionH,
        cfg.OffsetL, cfg.OffsetT, cfg.OffsetR, cfg.OffsetB,
        cfg.ShouldMaximize?1:0, cfg.TopMost?1:0, cfg.RemoveMenus?1:0,
        cfg.LockCursor?1:0, cfg.DisableWinKey?1:0,
        cfg.HideWindowsTaskbar?1:0, cfg.HideMouseCursor?1:0,
        cfg.OverrideDpi?1:0, cfg.EnableLog?1:0,
        cfg.UE3Mode?1:0, cfg.UE3RenderWidth, cfg.UE3RenderHeight,
        cfg.ToggleHotKey?1:0, cfg.ToggleHotKeyCode, cfg.ToggleHotKeyMod,
        cfg.ToggleCooldownMs, cfg.SendExitSizeMove?1:0,
        cfg.PollingIntervalMs, cfg.DelayMs);

    // 写入文件（UTF-8 编码）
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    int wlen = (int)wcslen(buf);
    char utf8[4096];
    int u8len = WideCharToMultiByte(CP_UTF8, 0, buf, wlen, utf8, sizeof(utf8), nullptr, nullptr);
    DWORD wr = 0;
    WriteFile(h, utf8, u8len, &wr, nullptr);
    CloseHandle(h);
    return wr > 0;
}
