# BorderLimited

Windows 游戏通用无边框全屏 ASI 插件。

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](../LICENSE) [![Platform](https://img.shields.io/badge/Platform-Windows%20x64%20%7C%20x86-lightgrey)]()

[English](README.md) | [中文](README_CN.md)

---

## 这是什么？

BorderLimited 是一个能将游戏的窗口变为无边框窗口（全屏）的ASI 插件，它不需要外部程序只通过 ASI Loader 注入游戏进程，自动应用配置，可搭配其他asi模组一起使用。

灵感来源于 [Borderless Gaming](https://github.com/Codeusa/Borderless-Gaming)，但重新设计为零配置、进程内运行的原生插件。

### 核心特性

- **即放即用**：如果你有ASI LOADER那么直接放进去就好了。
- **UE3 引擎支持**：UE3专用 D3D9 Hook 模式，兼容使用DX9的虚幻3游戏。
- **热键切换**：Alt+F10 切换无边框/窗口模式（键位可自定义，默认带2秒冷却保护）
- **附带配置用GUI**：可视化的 INI 编辑器，拖拽矩形定位窗口的预览覆盖层，一键切换语言

---

## 快速开始

1. 下载 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)，放到游戏EXE所在的目录
    - 虚幻引擎游戏需要放到Binaries文件夹内的EXE处，而不是根目录
2. 将 `BorderLimited.x64.asi`（64 位游戏）或 `BorderLimited.x86.asi`（32 位游戏）放到Ultimate ASI Loader同目录，或是它支持的 `scripts`/`plugins`/`update`目录里
    - 可选：`BorderLimited.ini`配置文件也一起放进游戏目录内，虽然不放插件也会自动生成。
3. 启动游戏，把游戏设置成窗口化即可，窗口自动变为无边框全屏

### 配置方式

- **GUI 方式**: 将 `BorderLimitedConfig.exe` 放到 ASI 同目录运行，可视化编辑
- **手动方式**: 用任意文本编辑器直接编辑 `BorderLimited.ini`
- **预览定位**: 打开预览覆盖层，拖拽红色矩形精确定位窗口

---

## 工作原理

### 轮询模式（默认，适用非虚幻引擎的游戏，或许虚幻4&5也可以？）

插件在后台创建工作线程，定期扫描当前进程的主游戏窗口。找到后通过 `SetWindowLong` + `SetWindowPos` 移除标题栏、边框和菜单样式，然后将窗口扩展到显示器全屏。

### UE3 Hook 模式（UE3Mode=1，虚幻3游戏必须开启）

虚幻引擎 3 游戏会持续恢复窗口样式，并使用独占全屏模式（Windowed=FALSE）创建 D3D9 设备。轮询模式无法跟上。UE3 模式通过 MinHook 安装 API 钩子实时拦截：

| 钩子目标 | 所在 DLL | 效果 |
|----------|---------|------|
| `SetWindowLongA/W` | `user32.dll` | 实时清除边框/标题栏样式位（与轮询路径共享统一样式掩码） |
| `SetWindowPos` | `user32.dll` | 强制游戏主窗口铺满显示器（仅拦截主窗口，子窗口不受影响） |
| `Direct3DCreate9` | `d3d9.dll` | VTable 补丁 `CreateDevice`（强制 Windowed=TRUE，失败时还原 pParams）和 `GetAdapterDisplayMode`（伪造分辨率），VTable 按实例去重兼容 DXVK |
| **热键** | — | Alt+F10 切换 Hook 开关——关闭时所有 Hook 直通原始 API，游戏恢复原生行为 |

---

## 配置项完整参考

ini示例文件在: [BorderLimited.ini](workspace\example\BorderLimited.ini)

| 分类 | 配置项 | 默认值 | 说明 |
|------|-------|--------|------|
| **尺寸** | `Size` | `FullScreen` | `FullScreen`(全屏) / `Custom`(自定义) / `Borderless`(无边框窗口) |
| | `PositionX/Y/W/H` | `0` | 自定义坐标（仅 Custom 模式） |
| | `OffsetL/T/R/B` | `0` | 四边偏移（正值收缩，负值扩展） |
| **行为** | `ShouldMaximize` | `1` | 去边框后最大化 |
| | `TopMost` | `0` | 窗口始终置顶 |
| | `RemoveMenus` | `0` | 移除 Win32 菜单栏（不可逆） |
| | `LockCursor` | `0` | ClipCursor 锁定鼠标 |
| | `DisableWinKey` | `0` | 键盘钩子拦截 Win 键 |
| | `HideWindowsTaskbar` | `0` | 隐藏任务栏 + 扩展工作区 |
| | `HideMouseCursor` | `0` | 替换为透明空白光标 |
| | `OverrideDpi` | `0` | 禁用 DPI 虚拟化模糊缩放 |
| | `EnableLog` | `1` | 写入日志文件（UTF-8，每次启动清空） |
| **引擎** | `UE3Mode` | `0` | 启用 D3D9 Hook 路径（UE3 游戏） |
| | `UE3RenderWidth/Height` | `0` | 伪造渲染分辨率（0=自动） |
| **热键** | `ToggleHotKey` | `1` | 启用热键切换 |
| | `ToggleHotKeyCode` | `121` | 热键虚拟键码（VK_F10） |
| | `ToggleHotKeyMod` | `1` | 修饰键掩码（1=Alt, 2=Ctrl, 4=Shift, 8=Win） |
| | `ToggleCooldownMs` | `2000` | 冷却间隔（防连按闪烁） |
| | `SendExitSizeMove` | `0` | 去边框后发送WM_EXITSIZEMOVE欺骗老引擎 |
| **高级** | `PollingIntervalMs` | `1000` | 窗口检测轮询间隔（0=仅执行一次） |
| | `DelayMs` | `0` | 样式设置延迟（GameMaker 引擎建议 2000） |

---

## 构建

### 环境要求

- Windows 10+
- Visual Studio 2019（MSVC 14.29）或兼容版本
- Windows SDK 10.0.26100.0+
- [MinHook](https://github.com/TsudaKageyu/minhook) 源码（BSD 2-Clause，已包含在仓库中）

### 构建 ASI 插件

```bash
bash workspace/scripts/build.sh
```

产物：`workspace/build/BorderLimited.x64.asi` + `BorderLimited.x86.asi`

### 构建配置 GUI

```bash
bash workspace/scripts/build_gui.sh
```

产物：`workspace/build/BorderLimitedConfig.exe`（约 700KB，单文件）

---

## 目录结构

```
workspace/
├── src/
│   ├── BorderLimited/           # ASI 插件源码 (C++)
│   │   ├── main.cpp             #   DllMain 入口 + 工作线程
│   │   ├── config.h / .cpp      #   INI 解析器
│   │   ├── window.h / .cpp      #   窗口操作实现
│   │   ├── ue3.h / .cpp         #   UE3 D3D9 Hook 模式
│   │   └── native.h             #   Win32 API 辅助函数
│   └── BorderLimitedConfig/     # 配置 GUI 源码 (C++/Win32)
│       ├── main.cpp             #   主对话框 + 预览窗口
│       ├── config_io.h / .cpp   #   INI 读写
│       ├── lang.h               #   中英文语言包
│       └── BorderLimitedConfig.rc # 对话框资源（VS 可视化编辑）
├── scripts/
│   ├── build.sh                 # ASI 构建脚本 (x64 + x86)
│   └── build_gui.sh             # GUI 构建脚本
├── archive/                     # 历史版本快照
└── build/                       # 构建产物
```

---

## 故障排查

**ASI 未加载？** 确认 Ultimate ASI Loader（`dinput8.dll` 或 `version.dll`）已放入游戏目录。

**窗口无变化？** 检查游戏目录下的 `BorderLimited.log`。如果 `EnableLog=0`，使用 [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) 查看实时调试输出。

**UE3 游戏窗口缩小到角落？** 在 INI 中设置 `UE3Mode=1`。

**配置修改不生效？** 用配置 GUI 重新保存一次 INI，确保 UTF-8 编码正确。

---

## 许可证

BorderLimited 采用 **MIT 许可证**。详见 [LICENSE](../LICENSE)。

本项目包含 [MinHook](https://github.com/TsudaKageyu/minhook)（BSD 2-Clause, Copyright (C) 2009-2017 Tsuda Kageyu）。

---

## 致谢

- [Borderless Gaming](https://github.com/Codeusa/Borderless-Gaming) by AndrewMD5 — 原始概念
- [GeDoSaTo](https://github.com/PeterTh/gedosato) by Durante — UE3 Hook 架构参考
- [SRWE](https://github.com/dtzxporter/SRWE) by dtzxporter — XOR 位清除技巧 + WM_EXITSIZEMOVE 启发
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu — API Hook 库
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG — ASI 插件加载器
- 项目图标 夫人的随手涂鸦
