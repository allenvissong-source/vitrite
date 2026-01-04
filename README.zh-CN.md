# Vitrite (现代化重制版)

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![License](https://img.shields.io/badge/license-GPL-green)
![Build](https://img.shields.io/badge/build-VS2022-purple)

**Vitrite** 是一个小巧的 Windows 实用工具，允许通过全局快捷键通过手动调整任意窗口的透明度。它还支持将窗口设置为“总在最前”（置顶）。

> [!NOTE]
> **重制项目说明**: 本项目是 Ryan VanMiddlesworth 原作（约 2002 年）的现代化 Fork 版本。针对 Windows 10 和 11 进行了原生重写，提升了安全性和稳定性。

[English Documentation (英文文档)](README.md)

## ✨ 现代化特性

此版本已根据现代 Windows 标准进行了全面重写：

*   **🛡️ 更安全的实现**: 移除了传统的 DLL 注入和全局键盘钩子，改用标准的 `RegisterHotKey`。这不仅更稳定，而且不会触发反病毒软件的误报。
*   **👁️ 高 DPI 支持**: 完整的 Per-Monitor V2 DPI 感知。在 4K 屏幕和多显示器混排环境下，UI 显示依然清晰锐利。
*   **🛡️ 崩溃恢复**: 若 Windows 资源管理器崩溃或重启，托盘图标会自动恢复。
*   **🌓 深色模式支持**: “关于”对话框会尝试适配系统的深色模式（Windows 10/11）。
*   **🌐 国际化支持**: 内置英文、简体中文和日语资源，根据系统 UI 语言自动切换。
*   **🛠️ 64 位支持**: 包含针对 x86 和 x64 架构的 Release 构建配置。

## ⌨️ 快捷键

| 快捷键 | 功能 |
| :--- | :--- |
| **Ctrl + Shift + 1..9** | 设置透明度级别 (1 = 10%, 9 = 90%, 0 = 重置) |
| **Ctrl + Shift + 0** | 将窗口重置为完全不透明 (移除透明效果)* |
| **Ctrl + Shift + +** | 切换 **总在最前** (使用主键盘区的 `+` 键) |
| **Ctrl + Shift + Num+** | 切换 **总在最前** (使用小键盘区的 `+` 键) |

*\*注意：为了安全起见，重置功能仅对本次运行中被 Vitrite 修改过的窗口生效。*

## 🏗️ 构建指南

**环境要求**: Visual Studio 2022 (Community 或更高版本)，需安装 C++ 桌面开发工作负载。

1.  打开 `src\Vitrite.sln`。
2.  选择目标架构（推荐 `x64`）。
3.  生成解决方案。

可执行文件将输出到 `x64\Release` (或 `Release`) 目录。

## 📜 致谢

*   **原作者**: Ryan VanMiddlesworth (2002)
*   **许可证**: GNU General Public License (GPL)
