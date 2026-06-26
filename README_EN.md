# BorderLimited

A universal borderless fullscreen tool for Windows games, delivered as an ASI plugin.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](../LICENSE) [![Platform](https://img.shields.io/badge/Platform-Windows%20x64%20%7C%20x86-lightgrey)]()

[中文](README.md) | English

---

## What is this?

BorderLimited is an ASI plugin that turns windowed games into borderless fullscreen — no external program needed. It's injected directly into the game process by an ASI Loader and applies the configuration automatically. Works alongside other ASI mods.

Inspired by [Borderless Gaming](https://github.com/Codeusa/Borderless-Gaming), but reimagined as a zero-config, process-native plugin.

### Key Features

- **Drop-in ready** — if you already have an ASI Loader, just drop the file in.
- **UE3 Engine support** — dedicated D3D9 Hook mode for Unreal Engine 3 games using DX9
- **Hotkey toggle** — Alt+F10 toggles borderless/windowed mode (customizable key, 2s cooldown by default)
- **Config GUI included** — visual INI editor with drag-to-position preview overlay, language toggle

---

## Quick Start

1. Download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) and place it next to the game executable
   - For Unreal Engine games, place it next to the EXE inside the `Binaries` folder, not the root directory
2. Copy `BorderLimited.x64.asi` (64-bit games) or `BorderLimited.x86.asi` (32-bit games) into the same folder as the ASI Loader, or into its supported `scripts`/`plugins`/`update` directories
   - Optional: include `BorderLimited.ini` — the plugin auto-generates one if missing
3. Launch the game and set it to windowed mode — the window automatically becomes borderless fullscreen

### Configuration

- **GUI**: Run `BorderLimitedConfig.exe` next to the ASI file for visual editing
- **Manual**: Edit `BorderLimited.ini` with any text editor
- **Preview overlay**: Drag a red rectangle to position your window precisely

---

## How It Works

### Polling Mode (default, for non-UE3 games; UE4/5 may also work)

The plugin spawns a background thread that periodically scans for the game's main window. When found, it strips the caption, border, and menu styles via `SetWindowLong` + `SetWindowPos`, then expands the window to fill the monitor.

### UE3 Hook Mode (UE3Mode=1, required for Unreal Engine 3 games)

Unreal Engine 3 games aggressively restore window styles and create exclusive fullscreen D3D9 devices. The polling approach can't keep up. UE3 Mode installs API hooks (via MinHook) to intercept:

| Hook | Target | Effect |
|------|--------|--------|
| `SetWindowLongA/W` | `user32.dll` | Strips border/caption style bits in real-time |
| `SetWindowPos` | `user32.dll` | Forces full-monitor geometry |
| `Direct3DCreate9` | `d3d9.dll` | VTable patches `CreateDevice` (force Windowed=TRUE) and `GetAdapterDisplayMode` (fake resolution) |

---

## Configuration Reference

ini example file to : [BorderLimited.ini](workspace/example/BorderLimited.ini)

| Section | Key | Default | Description |
|---------|-----|---------|-------------|
| **Size** | `Size` | `FullScreen` | `FullScreen` / `Custom` / `Borderless` |
| | `PositionX/Y/W/H` | `0` | Custom coordinates (Custom mode only) |
| | `OffsetL/T/R/B` | `0` | Edge offsets in pixels (negative = expand) |
| **Behavior** | `ShouldMaximize` | `1` | Maximize after borderless |
| | `TopMost` | `0` | Keep window always on top |
| | `RemoveMenus` | `0` | Remove Win32 menu bar (irreversible) |
| | `LockCursor` | `0` | ClipCursor to window bounds |
| | `DisableWinKey` | `0` | Suppress Windows key via keyboard hook |
| | `HideWindowsTaskbar` | `0` | Hide taskbar + expand work area |
| | `HideMouseCursor` | `0` | Replace cursor with transparent blank |
| | `OverrideDpi` | `0` | Disable DPI virtualization blur |
| | `EnableLog` | `1` | Write BorderLimited.log (UTF-8, cleared each launch) |
| **Engine** | `UE3Mode` | `0` | Enable D3D9 Hook path for UE3 games |
| | `UE3RenderWidth/Height` | `0` | Fake render resolution (0 = auto) |
| **Hotkey** | `ToggleHotKey` | `1` | Enable hotkey toggle |
| | `ToggleHotKeyCode` | `121` | Virtual key code (VK_F10) |
| | `ToggleHotKeyMod` | `1` | Modifier mask (1=Alt, 2=Ctrl, 4=Shift, 8=Win) |
| | `ToggleCooldownMs` | `2000` | Cooldown interval (anti-rapid-toggle) |
| | `SendExitSizeMove` | `0` | Send WM_EXITSIZEMOVE after borderless |
| **Advanced** | `PollingIntervalMs` | `1000` | Window detection interval (0 = one-shot) |
| | `DelayMs` | `0` | Pre-style delay (GameMaker: 2000) |

---

## Building

### Requirements

- Windows 10+
- Visual Studio 2019 (MSVC 14.29) or compatible
- Windows SDK 10.0.26100.0+
- [MinHook](https://github.com/TsudaKageyu/minhook) source placed in `../minhook-master/` (BSD 2-Clause, included as a dependency)

### Build ASI Plugin

```bash
bash workspace/scripts/build.sh
```

Output: `workspace/build/BorderLimited.x64.asi` + `BorderLimited.x86.asi`

### Build Config GUI

```bash
bash workspace/scripts/build_gui.sh
```

Output: `workspace/build/BorderLimitedConfig.exe` (~700KB, single-file)

---

## Project Structure

```
workspace/
├── src/
│   ├── BorderLimited/           # ASI plugin source (C++)
│   │   ├── main.cpp             #   DllMain + worker thread
│   │   ├── config.h / .cpp      #   INI parser
│   │   ├── window.h / .cpp      #   Window operations
│   │   ├── ue3.h / .cpp         #   UE3 D3D9 Hook mode
│   │   └── native.h             #   Win32 API helpers
│   └── BorderLimitedConfig/     # Config GUI source (C++/Win32)
│       ├── main.cpp             #   Dialog + preview overlay
│       ├── config_io.h / .cpp   #   INI read/write
│       ├── lang.h               #   CN/EN language pack
│       └── BorderLimitedConfig.rc # Dialog resource (VS editable)
├── scripts/
│   ├── build.sh                 # ASI build (x64 + x86)
│   └── build_gui.sh             # GUI build
├── archive/                     # Historical snapshots
└── build/                       # Build outputs
```

---

## Troubleshooting

**ASI not loading?** Make sure Ultimate ASI Loader (`dinput8.dll` or `version.dll`) is in the game folder.

**Window unchanged?** Check `BorderLimited.log` in the game directory. If `EnableLog=0`, use [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) to see live debug output.

**UE3 game shrinks to corner?** Set `UE3Mode=1` in the INI.

**Config changes ignored?** Re-save the INI with the Config GUI to ensure correct UTF-8 encoding.

---

## License

BorderLimited is licensed under the **MIT License**. See [LICENSE](../LICENSE) for details.

This project includes [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-Clause, Copyright (C) 2009-2017 Tsuda Kageyu).

---

## Acknowledgments

- [Borderless Gaming](https://github.com/Codeusa/Borderless-Gaming) by AndrewMD5 — original concept
- [GeDoSaTo](https://github.com/PeterTh/gedosato) by Durante — UE3 Hook architecture reference
- [SRWE](https://github.com/dtzxporter/SRWE) by dtzxporter — XOR bit-clear technique & WM_EXITSIZEMOVE inspiration
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu — API hooking library
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG — ASI plugin loader
- Icon My wife's Rakugaki
