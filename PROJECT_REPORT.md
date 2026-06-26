# BorderLimited — 项目总报告

> 生成日期: 2026-06-26  
> 项目类型: 原创项目 (MIT 许可证)  
> 当前版本: v1.2  
> 目标用户: 游戏玩家 / 模组开发者 / 游戏兼容性工程师  

---

## 一、项目概述

**BorderLimited** 是一套通用游戏无边框全屏工具，由两个独立程序组成：

1. **ASI 插件** (`BorderLimited.x64.asi` / `BorderLimited.x86.asi`)
   由 Ultimate ASI Loader 注入游戏进程，自动将游戏窗口转为无边框全屏。
   支持两种运行模式：默认轮询模式（WorkerThread）和针对虚幻引擎 3 的 D3D9 Hook 模式（MinHook API 拦截）。两种模式共享独立热键线程（HotkeyThread），Alt+F10 可随时切换无边框/窗口状态。

2. **配置 GUI** (`BorderLimitedConfig.exe`)
   独立的 Windows 桌面程序，提供可视化界面编辑 INI 配置文件。
   中英双语、悬浮提示、全屏预览拖拽定位。

### 设计哲学

- **零配置可用**：无 INI 文件时自动生成默认配置，开箱即用
- **进程内注入**：ASI 在游戏进程内运行，不依赖外部程序
- **单文件分发**：所有编译产物静态链接 CRT，游戏不需要安装运行库
- **文本配置**：纯文本 INI 格式，人可读可写，GUI 和非 GUI 均兼容

---

## 二、目录结构

```
BorderLimited-src/
├── LICENSE                          # MIT 许可证
├── PROJECT_REPORT.md                # 本文件
├── icon.ico                         # 应用程序图标
├── minhook-master/                  # MinHook 库 (BSD 2-Clause)
├── workspace/
│   ├── src/
│   │   ├── BorderLimited/           # ASI 插件源码 (C++)
│   │   │   ├── native.h             #   Win32 API 辅助 / 窗口样式常量
│   │   │   ├── config.h             #   配置数据结构 / INI 解析器声明
│   │   │   ├── config.cpp           #   INI 解析器实现
│   │   │   ├── window.h             #   窗口操作 API 声明
│   │   │   ├── window.cpp           #   窗口操作实现 (去边框/光标/DPI/钩子)
│   │   │   ├── ue3.h                #   UE3 D3D9 Hook 模式声明
│   │   │   ├── ue3.cpp              #   UE3 D3D9 Hook 实现
│   │   │   ├── main.cpp             #   DllMain 入口 / 轮询工作线程
│   │   │   └── BorderLimited.ini    #   默认 INI 模板
│   │   └── BorderLimitedConfig/     # 配置 GUI 源码 (C++/Win32)
│   │       ├── main.cpp             #   主对话框 + 预览窗口
│   │       ├── config_io.h          #   INI 读写接口
│   │       ├── config_io.cpp        #   INI 读写实现
│   │       ├── lang.h               #   中英文语言包
│   │       ├── resource.h           #   控件 ID 定义
│   │       ├── BorderLimitedConfig.rc # 对话框资源 (VS 可视化编辑)
│   │       ├── BorderLimitedConfig.sln / .vcxproj  # VS 项目文件
│   │       └── manifest.xml         #   DPI 感知 + Common Controls v6 清单
│   ├── scripts/
│   │   ├── build.sh                 # ASI 双架构编译脚本
│   │   └── build_gui.sh             # Config GUI 编译脚本
│   ├── build/                       # 编译产物目录
│   ├── archive/                     # 历史版本存档
│   │   ├── pre-ue3_src/             #   v1.0: 无 UE3 支持
│   │   └── pre-ui-refresh_src/      #   Config GUI 存档版本
│   └── temp/                        # 编译中间文件
└── rounds/                          # 开发轮次标记
```

---

## 三、技术架构 (ASI 插件)

### 3.1 运行模式

#### 默认轮询模式 (UE3Mode=0)

```
DllMain → 加载 INI → 创建 HotkeyThread（双模式共用）→ 创建工作线程（仅非UE3）
                        ↓
              Sleep(500ms) 初始等待
                        ↓
              循环 (每 PollingIntervalMs):
                ├─ 检查已知窗口 (g_hLastWindow)
                │   ├─ 正常 → 跳过
                │   ├─ 样式恢复 → 重新应用
                │   └─ 窗口销毁 → 重新搜索
                └─ 搜索新窗口 (EnumWindows)
                    └─ 找到 → ApplyBorderless
```

**适用场景**: 99% 的非 UE3 游戏

#### UE3 Hook 模式 (UE3Mode=1)

```
DllMain → 加载 INI → MinHook_Initialize
                        ↓
              安装 Hook:
              ├─ SetWindowLongA/W → AdjustStyleBits (清除边框位)
              ├─ SetWindowPos      → 强制全屏坐标
              └─ Direct3DCreate9   → VTable Patch:
                    ├─ CreateDevice         → 强制 Windowed=TRUE
                    └─ GetAdapterDisplayMode → 伪造分辨率
              (无轮询线程)
```

**适用场景**: 虚幻引擎 3 游戏 (Mass Effect, BioShock, Borderlands 等)

### 3.2 关键技术点

| 技术 | 说明 | 对应文件 |
|------|------|---------|
| 窗口样式位掩码 | `style & ~StylesToRemove` 清除边框/标题栏位 | native.h / window.cpp |
| 窗口跟踪 | 保存 HWND, IsWindow 检查存活, IsAlreadyBorderless 检查样式 | main.cpp |
| 编码自适应 | UTF-8 优先, 回退 ANSI (CP_ACP), BOM 跳过 | config.cpp |
| 内联注释支持 | `;` 到行尾的内容视为注释并剥离 | config.cpp |
| 自动 INI 生成 | 无 INI 时创建默认配置并继续运行 | config.cpp |
| 线程安全退出 | atomic\<bool\> + WaitForSingleObject + TerminateThread 兜底 | main.cpp |
| 异常保护 | __try/__except 包裹轮询循环, 防止单次失败导致线程崩溃 | main.cpp |
| MinHook API Hook | 运行时动态替换目标函数入口, 无需修改原始代码 | ue3.cpp |
| VTable 补丁 | 直接修改 COM 对象虚函数表, 拦截特定接口方法 | ue3.cpp |

### 3.3 配置项完整列表 (23 项)

| 分类 | 配置项 | 默认值 | 说明 |
|------|-------|--------|------|
| A.尺寸 | `Size` | FullScreen | FullScreen / Custom / Borderless |
| | `PositionX/Y/W/H` | 0 | 自定义窗口坐标 (Custom 模式) |
| | `OffsetL/T/R/B` | 0 | 四边偏移 (正值收缩, 负值扩展) |
| B.行为 | `ShouldMaximize` | 1 | 去边框后最大化 |
| | `TopMost` | 0 | 窗口置顶 |
| | `RemoveMenus` | 0 | 移除 Win32 菜单栏 (不可逆) |
| | `LockCursor` | 0 | ClipCursor 锁定鼠标 |
| | `DisableWinKey` | 0 | WH_KEYBOARD_LL 拦截 Win 键 |
| | `HideWindowsTaskbar` | 0 | 隐藏任务栏 + 扩展工作区 |
| | `HideMouseCursor` | 0 | 透明光标替换 |
| | `OverrideDpi` | 0 | 禁用 DPI 虚拟化缩放 |
| | `EnableLog` | 1 | 写入日志文件 |
| D.热键 | `ToggleHotKey` | 1 | 启用热键切换 |
| | `ToggleHotKeyCode` | 121 (VK_F10) | 热键虚拟键码 |
| | `ToggleHotKeyMod` | 1 (MOD_ALT) | 修饰键掩码 (1=Alt,2=Ctrl,4=Shift,8=Win) |
| | `ToggleCooldownMs` | 2000 | 热键冷却间隔 (ms) |
| | `SendExitSizeMove` | 0 | 去边框后发送 WM_EXITSIZEMOVE |
| C.引擎 | `UE3Mode` | 0 | 启用 D3D9 Hook 模式 |
| | `UE3RenderWidth/Height` | 0 | UE3 伪造分辨率 (0=自动) |
| D.高级 | `PollingIntervalMs` | 1000 | 轮询间隔 (0=单次) |
| | `DelayMs` | 0 | 样式设置延迟 |

---

## 四、技术架构 (Config GUI)

### 4.1 架构

```
WinMain → SetProcessDpiAwarenessContext → DialogBoxParamW
                                              ↓
                                         DlgProc (主对话框)
                                           ├─ WM_INITDIALOG: 创建控件/提示/加载配置
                                           ├─ WM_COMMAND:   按钮/复选框/单选事件
                                           ├─ WM_APP:       预览窗口关闭通知
                                           └─ Preview按钮 → ShowPreview()
                                                            ↓
                                                       PvProc (预览窗口)
                                                         ├─ 拖拽移动/缩放
                                                         ├─ 方向键微调 + 定时器加速
                                                         ├─ Enter/中键居中
                                                         ├─ Ctrl+S 保存 & 关闭
                                                         └─ Esc 取消 & 关闭
```

### 4.2 技术栈

| 技术 | 用途 |
|------|------|
| DialogBoxParamW | 模态对话框 (从 .rc 资源加载) |
| Visual Studio 对话框编辑器 | 可视化拖拽调整控件布局 |
| Common Controls v6 | 现代 Windows 控件外观 (通过 manifest) |
| PerMonitorV2 DPI | 多屏高 DPI 坐标准确 |
| TOOLTIPS_CLASS | 鼠标悬浮提示 |
| Layered Window | 预览窗口半透明效果 |
| Memory DC + BitBlt | 预览窗口双缓冲消除闪烁 |

### 4.3 预览窗口操作完整说明

| 操作 | 快捷键 | 效果 |
|------|-------|------|
| 拖动矩形内部 | 鼠标左键 | 移动矩形位置 (改 X/Y) |
| 拖动矩形边缘 (8px 范围) | 鼠标左键 | 调整矩形大小 (改 W/H) |
| 锁定水平/垂直轴 | Shift / Ctrl | 拖拽时限制只沿一个轴移动 |
| 居中矩形 | Enter / 鼠标中键 | 矩形居中于当前显示器 |
| 逐像素移动 | 方向键 (点按) | 移动 1px |
| 持续移动 (加速) | 方向键 (按住) | 1.5s→2px, 3s→5px, 5s→10px |
| 保存并关闭 | Ctrl+S | 写入配置, 关闭预览 |
| 取消并关闭 | Esc | 丢弃修改, 关闭预览 |

---

## 五、构建指南

### 5.1 环境要求

- Windows 10 或更高版本
- Visual Studio 2019 (MSVC 14.29) 或兼容版本
- Windows SDK 10.0.26100.0 或更高版本
- Git Bash (用于运行构建脚本)

### 5.2 构建 ASI 插件

```bash
cd borderlessgaming-asi
bash workspace/scripts/build.sh
```

产物:
- `workspace/build/BorderLimited.x64.asi` (64位)
- `workspace/build/BorderLimited.x86.asi` (32位)

### 5.3 构建 Config GUI

```bash
bash workspace/scripts/build_gui.sh
```

产物:
- `workspace/build/BorderLimitedConfig.exe` (单文件, ~700KB)

### 5.4 编译选项说明

所有产物均使用 `/MT` (静态链接 CRT), 目标游戏/系统不需要安装 Visual C++ Redistributable。

---

## 六、使用指南

### 6.1 ASI 插件安装

1. 下载并安装 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
   (通常是将 `dinput8.dll` 或 `version.dll` 放入游戏目录)
2. 将 `BorderLimited.x64.asi` (64位游戏) 或 `BorderLimited.x86.asi` (32位游戏)
   放入游戏 EXE 同目录
3. 将 `BorderLimited.ini` 放入同一目录
4. 启动游戏 — 窗口自动变为无边框全屏

### 6.2 配置方式

- **GUI 方式**: 运行 `BorderLimitedConfig.exe` → 调整参数 → 保存
- **手动方式**: 用文本编辑器直接编辑 `BorderLimited.ini`
- **首次运行**: 如果没有 INI 文件，ASI 会自动生成一份默认配置

### 6.3 UE3 模式启用

1. 在 INI 中设置 `UE3Mode=1`
2. 将 ASI 放入 UE3 游戏的 EXE 目录
3. 启动游戏 — 无需任何额外操作

注意: UE3 模式和普通轮询模式互斥。非 UE3 游戏不需要开启 UE3Mode。

---

## 七、故障排查

### 7.1 检查日志

游戏目录下会自动生成 `BorderLimited.log` (UTF-8 文本, 可用记事本打开)。
如果 `EnableLog=0`, 日志不会写入文件但可通过 Sysinternals DebugView 实时查看。

### 7.2 常见问题

| 症状 | 可能原因 | 解决方案 |
|------|---------|---------|
| ASI 未加载 | ASI Loader 未安装或版本不对 | 确认 dinput8.dll/version.dll 在游戏目录 |
| 窗口无变化 | 游戏窗口类名不在检测范围 | 查看日志确认是否找到窗口 |
| 窗口缩小到角落 | UE3 Mode 未启用 | 对 UE3 游戏启用 UE3Mode=1 |
| 配置不生效 | INI 编码错误 | 用 GUI 重新保存一次 INI |
| 双屏光标飘走 | LockCursor 未启用 | 设置 LockCursor=1 |
| 日志文件乱码 | 早期版本 UTF-16 问题 | 更新到最新版本（使用 UTF-8） |

### 7.3 调试工具

- [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview): 实时查看 OutputDebugString 日志
- [Spy++](https://learn.microsoft.com/en-us/visualstudio/debugger/introducing-spy-increment): 查看窗口样式和类名
- Visual Studio 对话框编辑器: 可视化调整 Config GUI 布局

---

## 八、版本历史

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| v1.0 | 2026-06-16 | 初始版本: 轮询模式 + 16 项 INI 配置 |
| v1.1 | 2026-06-17 | UE3 D3D9 Hook 模式 + Config GUI + 18 项修复 |
| v1.2 | 2026-06-26 | 热键切换 + 双模式 HotkeyThread + 8轮代码审查(40+ bug) + DXVK VTable兼容 + 28项配置 |

归档位置: `workspace/archive/`

---

## 九、许可和依赖

- **BorderLimited** 本体: MIT 许可证
- **MinHook**: BSD 2-Clause 许可证 (Tsuda Kageyu, 2009-2017)
  - 项目内路径: `minhook-master/`
  - GitHub: https://github.com/TsudaKageyu/minhook
- **技术参考**: Borderless Gaming (GPL v2, AndrewMD5) — 仅参考设计思路, 无代码复用

---

## 十、未来扩展方向

1. **DXGI Hook 支持** (D3D10/D3D11 UE3 游戏) — 功能已调研, 架构预留完成
2. **热键切换** — `RegisterHotKey` + `WM_HOTKEY` 消息处理