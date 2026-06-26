# Changelog

[English](CHANGELOG.md) | [中文](CHANGELOG_CN.md)

## v1.2 (2026-06-26)

### Added
- **Hotkey toggle**: Alt+F10 toggles borderless/windowed mode (dual-mode shared HotkeyThread)
  - Customizable virtual key code and modifier (Alt/Ctrl/Shift/Win)
  - Cooldown interval (ToggleCooldownMs) prevents rapid-toggle flicker
  - UE3 mode: toggles Hook active state (bypass → raw API → native behavior)
- **SendExitSizeMove**: Post-borderless WM_EXITSIZEMOVE tricks old engines (hotsampling)
- **Multi-strategy window finding**: EnumWindows + EnumThreadWindows fallback
- **UE3 Hook enhancements**:
  - Per-instance VTable dedup (DXVK-compatible)
  - HkSetWindowPos filters to main game window only
  - pParams restoration on CreateDevice failure
- **Config GUI extended options dialog**: visual hotkey settings editor

### Fixed (8 review rounds, 40+ bugs)
- **Data races**: g_hLastWindow → std::atomic\<HWND\>; g_bHooksActive/g_bCursorHidden/g_dwHookThreadId/s_firstWindowDone/s_firstDeviceDone/s_d3d9Patched → std::atomic; g_savedState atomic ready flag
- **UE3 silent failure**: d3d9.dll not loaded → UE3::Init returns false → WorkerThread fallback; user32 Hook install order swapped; all Hooks check return values
- **TOCTOU**: RestoreWindow double-check (pre-copy + post-copy acquire)
- **Restore robustness**: SaveOriginalState overwrites each cycle; IsValidSavedState uses rect area
- **WinKey Hook**: intercept WM_KEYDOWN only (release WM_KEYUP); WM_QUIT thread exit
- **HideMouseCursor**: CAS atomic swap prevents multi-thread HCURSOR leak
- **Logging**: clear after config load (respects EnableLog=0); UTF-8 buffer 4KB→8KB
- **INI**: off-by-one resize(len-1)→resize(len); encoding failure OutputDebugString; Version comment removed
- **Style masks**: polling and UE3 paths unified via native.h WindowStyle constants
- **Build**: MinHook copied to temp dir (Chinese path workaround); cmd batch wraps MSVC

## v1.1-dev (2026-06-17)

### Added
- **UE3 Mode**: D3D9 API Hook path for Unreal Engine 3 games (MinHook integration)
  - Hooks `SetWindowLongA/W`, `SetWindowPos`, `Direct3DCreate9`
  - VTable patches `IDirect3D9::CreateDevice` (force Windowed=TRUE) and `GetAdapterDisplayMode` (fake resolution)
- **Config GUI** (`BorderLimitedConfig.exe`):
  - Visual INI editor with all 23 settings
  - Dialog resource layout editable in Visual Studio dialog editor
  - Preview overlay: drag rectangle, arrow-key nudging with acceleration, Enter/MMB centering, Ctrl+S/Esc
  - Chinese/English bilingual UI with detailed tooltips on every control
  - PerMonitorV2 DPI awareness
  - Application icon embedded
- Project root `PROJECT_REPORT.md` — comprehensive technical documentation in Chinese
- Auto-generate default INI on first launch (no config file needed)
- Log file cleared at each game launch

### Fixed
- INI inline comment stripping (`;` comments)
- UTF-8 encoding detection with CP_ACP fallback
- UTF-8 BOM (EF BB BF) skipping
- Section boundary detection (`\n[` instead of bare `[`)
- Window handle tracking after borderless application (saved HWND fast path)
- Log throttling (once per 30s when no window found)
- Log encoding changed from UTF-16 to UTF-8
- DPI type definitions with WIN32_LEAN_AND_MEAN compatibility
- Worker thread safe shutdown (WaitForSingleObject + TerminateThread fallback)
- Conditional taskbar/cursor restoration on DLL unload
- Multi-byte conversion buffer size (off-by-one fix)
- Atomic thread flag (`std::atomic<bool>`)
- Mouse cursor transparency (AND mask correction)
- WinKey hook module handle (use DLL HMODULE instead of EXE)
- Preview window flicker (manual double-buffer via memory DC)

---

## v1.0 (2026-06-16)

### Initial Release
- Borderless fullscreen via `SetWindowLong` + `SetWindowPos`
- INI-based configuration with 16 settings
- Polling worker thread with configurable interval
- Window tracking (handle change + style restoration detection)
- Multi-monitor support via `MonitorFromWindow`
- UTF-8 logging with enable/disable toggle
- Cursor lock (`ClipCursor`)
- Windows key suppression (`WH_KEYBOARD_LL`)
- Taskbar hide/restore (`Shell_TrayWnd` + `SPI_SETWORKAREA`)
- Mouse cursor hide (transparent `SetSystemCursor`)
- DPI scaling override (3-tier fallback: PerMonitorV2 → PerMonitor → System)
- Menu bar removal
- Window maximize and topmost options
- Custom position/size with edge offsets
- Delayed style application for GameMaker/Unreal engines
- Dual architecture (x64 + x86) with static CRT linking
- One-shot mode (`PollingIntervalMs=0`)
