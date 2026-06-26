# Contributing to BorderLimited

[English](CONTRIBUTING.md) | [中文](CONTRIBUTING_CN.md)

Thanks for your interest in contributing! This document outlines the development workflow.

## Getting Started

1. **Clone** the repository
2. **Build** the ASI plugin and Config GUI (see [README_EN](README_EN.md#building))
3. **Test** on a real game before submitting changes

## Development Environment

- Windows 10+
- Visual Studio 2019 (MSVC 14.29) or compatible
- Windows SDK 10.0.26100.0+
- Git Bash (for build scripts)
- [MinHook](https://github.com/TsudaKageyu/minhook) source in the parent directory

## Project Layout

| Directory | Purpose |
|-----------|---------|
| `workspace/src/BorderLimited/` | ASI plugin (C++ DLL) |
| `workspace/src/BorderLimitedConfig/` | Config GUI (C++ EXE) |
| `workspace/scripts/` | Build scripts |
| `workspace/archive/` | Historical version snapshots |

## Code Style

- **Language**: C++17
- **Comments**: Chinese (primary) — English summaries acceptable in headers
- **Formatting**: Consistent with surrounding code
- **APIs**: Win32 only, no external libraries except MinHook (BSD 2-Clause)
- **Linking**: `/MT` (static CRT) — never introduce dynamic CRT dependencies
- **Character set**: Unicode (`wchar_t` throughout)

## Before Submitting

- [ ] Build passes for both x64 and x86 (`bash workspace/scripts/build.sh`)
- [ ] Config GUI builds without errors (`bash workspace/scripts/build_gui.sh`)
- [ ] Tested on at least one real game
- [ ] No new compiler warnings
- [ ] Comments added/updated for changed code
- [ ] INI format backward-compatible

## Commit Guidelines

Write commit messages in **Chinese**. Keep them concise and descriptive.

```
简短标题 (改动范围: 具体内容)

- 详细改动点 1
- 详细改动点 2
```

## Reporting Issues

When reporting bugs, please include:

- Game name and engine (if known)
- `BorderLimited.log` contents
- Relevant INI settings
- Windows version

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
