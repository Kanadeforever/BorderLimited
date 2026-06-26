# 参与贡献 BorderLimited

[English](CONTRIBUTING.md) | [中文](CONTRIBUTING_CN.md)

感谢你对本项目感兴趣！本文档说明开发流程和规范。

## 快速上手

1. **克隆**仓库
2. **构建** ASI 插件和配置 GUI（详见 [README](README.md#构建)）
3. **测试**：在真实游戏上验证后再提交修改

## 开发环境

- Windows 10+
- Visual Studio 2019（MSVC 14.29）或兼容版本
- Windows SDK 10.0.26100.0+
- Git Bash（用于运行构建脚本）
- [MinHook](https://github.com/TsudaKageyu/minhook) 源码（已包含在仓库中）

## 项目布局

| 目录 | 用途 |
|------|------|
| `workspace/src/BorderLimited/` | ASI 插件（C++ DLL） |
| `workspace/src/BorderLimitedConfig/` | 配置 GUI（C++ EXE） |
| `workspace/scripts/` | 构建脚本 |
| `workspace/archive/` | 历史版本快照 |

## 代码规范

- **语言标准**: C++17
- **注释**: 中文（主要）— 头文件中允许英文摘要
- **格式**: 与已有代码保持一致
- **API**: 仅限 Win32，除 MinHook（BSD 2-Clause）外不引入任何外部库
- **链接方式**: `/MT`（静态 CRT）— 绝不引入动态 CRT 依赖
- **字符集**: Unicode（全程使用 `wchar_t`）

## 提交前检查清单

- [ ] x64 和 x86 双架构编译通过（`bash workspace/scripts/build.sh`）
- [ ] 配置 GUI 编译无误（`bash workspace/scripts/build_gui.sh`）
- [ ] 至少在一款真实游戏上测试过
- [ ] 无新增编译器警告
- [ ] 修改的代码已添加或更新注释
- [ ] INI 格式向后兼容

## 提交规范

提交信息使用**中文**，简洁明了。

```
简短标题 (改动范围: 具体内容)

- 详细改动点 1
- 详细改动点 2
```

## 报告问题

提交 Bug 报告时请附带：

- 游戏名称和引擎（如果知道）
- `BorderLimited.log` 日志内容
- 相关 INI 配置
- Windows 版本

## 许可证

参与贡献即表示你同意将贡献代码以 MIT 许可证授权。
