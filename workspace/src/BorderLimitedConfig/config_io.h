#pragma once

// ===================================================================
// config_io.h — Config GUI 的 INI 文件读写接口
//
// 使用与 ASI 插件完全相同的 INI 格式，确保双向兼容。
// GUI 保存的 INI 可以直接被 ASI 插件读取，反之亦然。
//
// 数据结构:
//   ConfigData  — GUI 使用的配置结构体（与 ASI 的 AppConfig 布局一致）
//   WindowSize  — 窗口尺寸模式枚举（Borderless / FullScreen / Custom）
//
// 函数:
//   ConfigLoad  — 从 INI 文件路径加载配置
//   ConfigSave  — 将配置写入 INI 文件路径
// ===================================================================

#include <windows.h>
#include <string>

// 窗口尺寸枚举 — 与 ASI 插件的 WindowSize 定义保持一致
enum class WindowSize { Borderless = 0, FullScreen = 1, Custom = 2 };

// ConfigData — GUI 使用的运行时配置数据
// 字段布局和默认值与 ASI 插件的 AppConfig 结构体完全对齐
struct ConfigData {
    WindowSize Size = WindowSize::FullScreen;
    int PositionX = 0, PositionY = 0, PositionW = 0, PositionH = 0;
    int OffsetL = 0, OffsetT = 0, OffsetR = 0, OffsetB = 0;
    bool ShouldMaximize = true, TopMost = false, RemoveMenus = false;
    bool LockCursor = false, DisableWinKey = false;
    bool HideWindowsTaskbar = false, HideMouseCursor = false;
    bool OverrideDpi = false, EnableLog = true, UE3Mode = false;
    int  PollingIntervalMs = 1000, DelayMs = 0;
    int  UE3RenderWidth = 0, UE3RenderHeight = 0;
    bool ToggleHotKey = true;
    int  ToggleHotKeyCode = VK_F10;
    int  ToggleHotKeyMod = MOD_ALT;
    int  ToggleCooldownMs = 2000;
    bool SendExitSizeMove = false;
};

// 从 INI 文件加载配置。返回 true 表示成功，false 表示文件不存在或解析失败
bool ConfigLoad(const wchar_t* path, ConfigData& out);

// 将配置写入 INI 文件（UTF-8 编码），覆盖已有内容。返回 true 表示写入成功
bool ConfigSave(const wchar_t* path, const ConfigData& cfg);
