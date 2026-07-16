<p align="center">
  <img src="src/app-winui/Assets/branding/Folia.svg" width="156" alt="折笺图标">
</p>

<h1 align="center">折笺</h1>

<p align="center">
  一款安静、原生、忠于源码的 Windows Markdown 阅读与编辑器。
</p>

<p align="center">
  <a href="README.md">English</a>
</p>

<p align="center">
  <img alt="许可证：MIT" src="https://img.shields.io/badge/license-MIT-238AF5.svg">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C.svg">
  <img alt="平台：Windows" src="https://img.shields.io/badge/platform-Windows-0078D4.svg">
  <img alt="界面：WinUI 3" src="https://img.shields.io/badge/UI-WinUI%203-512BD4.svg">
  <img alt="渲染器：Direct2D" src="https://img.shields.io/badge/renderer-Direct2D-2F855A.svg">
</p>

---

折笺使用 WinUI 3 构建应用外壳，以 Direct2D 和 DirectWrite 自绘文档界面，
不嵌入 HTML 编辑器。编辑状态由统一块树和块内 source-backed lossless inline
CST 组成，因此结构化编辑不需要重新生成或规范化用户没有修改的 Markdown。

## 特点

- **忠于源码** — 保留标记写法、转义、空白、链接语法和暂时未闭合的编辑状态。
- **原生编辑** — 所见即所得与等宽源码模式、目录导航、事务历史和语义化
  复制粘贴均以原生 C++ 实现。
- **丰富渲染** — 支持 MathJax、Mermaid、Tree-sitter 代码高亮、动态 GIF、
  图片、表格、脚注、callout 与递归嵌套块。
- **专注阅读** — 支持可配置主题、中英文界面、无边框 WinUI 外壳和原生
  Windows PDF 导出。
- **清晰边界** — C++23 modules 核心保持平台无关；Windows、DirectX 和
  WinUI 代码仅存在于平台层和应用层。

## 架构

```text
Markdown 源码
    └─ 块树
        └─ 可编辑块
            ├─ 精确的块内源码
            └─ 逐字符无损的可编辑 CST
                 ↓
        平台无关的渲染模型
                 ↓
        WinUI 3 + Direct2D/DirectWrite
```

各层职责见 [源码目录说明](src/README.md)、[核心层说明](src/core/README.md)
与 [WinUI 应用层说明](src/app-winui/README.md)。

## 从源码构建

需要 Windows 10 或 11、带 MSVC 桌面开发工作负载的 Visual Studio、CMake、
Ninja、Rust/Cargo 和 PowerShell。

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1
powershell -ExecutionPolicy Bypass -File .\build_app.ps1 -Configuration Debug
```

执行干净的 Release 构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\build_app.ps1 -Configuration Release -Clean
```

构建当前用户安装的 x64 MSI：

```powershell
powershell -ExecutionPolicy Bypass -File .\build_msi.ps1 -Version 0.1.0
```

安装器会生成自包含 Windows App SDK 的 Release 应用，因此目标电脑无需另行
安装 Windows App SDK Runtime。MSI 输出到
`build/installer/bin/Folia-<version>-x64.msi`；程序安装在
`%LOCALAPPDATA%\Programs\Folia`，可配置的 Assets 树安装在
`%LOCALAPPDATA%\Folia\Assets`。卸载会移除 MSI 管理的内置资源，但保留
`settings.json` 和 `themes/custom/`。

安装包构建会通过编译参数把 Assets 根目录设为
`{LocalAppData}\Folia\Assets`，程序启动时再针对当前用户解析该占位符；普通
开发构建仍沿用原有 Assets 位置。

所有生成文件都位于 `build/`。应用输出位于
`build/app-winui/bin/<platform>/<configuration>/`，构建不会向源码树写入文件。

运行核心测试：

```powershell
cmd /c build_test.bat
```

WinUI 工程入口是 [Folia.vcxproj](src/app-winui/Folia.vcxproj)。自定义主题和
外部 Assets 根目录的说明见 [主题文档](docs/THEMES.md)。

## 项目状态

折笺仍在积极开发中。编辑器核心、原生渲染器与 WinUI 应用已可运行；单元测试
和随机属性测试会持续检查源码无损与编辑不变量。

## 许可证

折笺采用 [MIT License](LICENSE)。第三方依赖许可证见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 及其指向的完整分发清单。
