# 更新日志

[English](CHANGELOG.md) | [中文](CHANGELOG_CN.md)

## v1.2 (2026-06-26)

### 新增
- **热键切换**: Alt+F10 一键切换无边框/窗口模式（双模式共用，独立 HotkeyThread）
  - 可自定义虚拟键码和修饰键（Alt/Ctrl/Shift/Win）
  - 冷却间隔（ToggleCooldownMs）防连按闪烁
  - UE3 模式下热键切换 Hook 开关（直通原始 API 恢复原生行为）
- **SendExitSizeMove**: 去边框后发送 WM_EXITSIZEMOVE，欺骗老游戏引擎（hotsampling 场景）
- **多策略窗口查找**: EnumWindows + EnumThreadWindows 双回退
- **UE3 Hook 增强**:
  - VTable 按实例地址去重（兼容 DXVK 等替代 D3D9 实现）
  - HkSetWindowPos 仅拦截游戏主窗口（子窗口/对话框不受影响）
  - CreateDevice 失败时还原 pParams
- **Config GUI 扩展选项对话框**: 热键键码、修饰键、冷却间隔的可视化设置
- **预览窗口**: 数据验证（禁止无效数值 + Enter 键应用）、锁轴健壮性改进

### 修复 (8轮审查, 40+ bug)
- **数据竞争**: g_hLastWindow → std::atomic\<HWND\>; g_bHooksActive / g_bCursorHidden / g_dwHookThreadId / s_firstWindowDone / s_firstDeviceDone / s_d3d9Patched → std::atomic; g_savedState 增加 atomic ready flag
- **UE3 静默失效**: d3d9.dll 未加载时 UE3::Init 返回 false 触发 WorkerThread 回退; user32 Hook 安装顺序调换; 全部 Hook 增加返回值检查
- **TOCTOU**: RestoreWindow 双检（拷贝前 + 拷贝后 acquire），防止混合快照恢复
- **恢复健壮性**: SaveOriginalState 每次覆盖（不再一次性），IsValidSavedState 改用 rect 面积判断
- **WinKey Hook**: 仅拦截 WM_KEYDOWN（放行 WM_KEYUP），配备 WM_QUIT 线程退出机制
- **HideMouseCursor**: CAS 原子切换防止多线程 HCURSOR 句柄泄漏
- **日志**: 清空移到配置加载后（尊重 EnableLog=0）；UTF-8 缓冲区 4KB→8KB
- **INI**: off-by-one resize(len-1)→resize(len); 编码失败时 OutputDebugString 提示; Version 注释移除
- **样式掩码**: 轮询路径与 UE3 路径统一使用 native.h 中的 WindowStyle 常量
- **构建**: MinHook 复制到临时目录避免中文路径编码问题; 使用 cmd 批处理包装 MSVC 编译

## v1.1-dev (2026-06-17)

### 新增
- **UE3 模式**: 针对虚幻引擎 3 游戏的 D3D9 API Hook 路径（MinHook 集成）
  - 拦截 `SetWindowLongA/W`、`SetWindowPos`、`Direct3DCreate9`
  - VTable 补丁 `IDirect3D9::CreateDevice`（强制 Windowed=TRUE）和 `GetAdapterDisplayMode`（伪造分辨率）
- **配置 GUI** (`BorderLimitedConfig.exe`)：
  - 完整的可视化 INI 编辑器，覆盖全部 23 项配置
  - 对话框资源布局，支持 Visual Studio 对话框编辑器拖拽调整
  - 预览覆盖层：拖拽矩形、方向键微调带加速、Enter/中键居中、Ctrl+S 保存、Esc 取消
  - 中英双语界面，每项配置带详细悬浮提示
  - PerMonitorV2 DPI 感知
  - 嵌入应用程序图标
- 根目录 `PROJECT_REPORT.md` — 中文项目技术总报告
- 首次运行无 INI 时自动生成默认配置文件
- 每次启动游戏时清空旧日志

### 修复
- INI 内联注释剥离（`;` 注释支持）
- UTF-8 编码检测及 ANSI 代码页回退
- UTF-8 BOM (EF BB BF) 自动跳过
- 段落边界检测（使用 `\n[` 代替直接查找 `[`）
- 无边框后的窗口句柄跟踪（保存 HWND 快速路径）
- 日志限频（无窗口时每 30 秒只记一条）
- 日志编码从 UTF-16 改为 UTF-8
- WIN32_LEAN_AND_MEAN 下的 DPI 类型兼容
- 工作线程安全退出（WaitForSingleObject + TerminateThread 兜底）
- DLL 卸载时条件恢复任务栏和光标
- 多字节转宽字符缓冲区大小（off-by-one 修复）
- 线程标志改为原子变量（`std::atomic<bool>`）
- 鼠标光标透明掩码修正（AND 掩码）
- WinKey 钩子模块句柄（使用 DLL 的 HMODULE 而非 EXE 的）
- 预览窗口闪烁修复（手动双缓冲 via Memory DC + BitBlt）

---

## v1.0 (2026-06-16)

### 初始版本
- 通过 `SetWindowLong` + `SetWindowPos` 实现无边框全屏
- 基于 INI 配置，16 项可配置参数
- 轮询工作线程，可配置检测间隔
- 窗口跟踪（句柄变更 + 样式恢复检测）
- 多显示器支持（`MonitorFromWindow` 自动选择）
- UTF-8 日志文件，可开关
- 光标锁定（`ClipCursor`）
- Win 键禁用（`WH_KEYBOARD_LL`）
- 任务栏隐藏/恢复（`Shell_TrayWnd` + `SPI_SETWORKAREA`）
- 鼠标光标隐藏（透明 `SetSystemCursor`）
- DPI 缩放覆盖（三层回退：PerMonitorV2 → PerMonitor → System）
- 菜单栏移除
- 窗口最大化和置顶选项
- 自定义位置/尺寸，支持四边偏移
- GameMaker/Unreal 引擎的延迟样式设置
- 双架构（x64 + x86）静态 CRT 链接
- 单次模式（`PollingIntervalMs=0`）
