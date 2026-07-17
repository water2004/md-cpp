<p align="center">
  <img src="src/app-winui/Assets/branding/Folia.svg" width="156" alt="Folia logo">
</p>

<h1 align="center">Folia</h1>

<p align="center">
  A calm, native, source-faithful Markdown reader and editor for Windows.
</p>

<p align="center">
  <a href="README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-238AF5.svg">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C.svg">
  <img alt="Platform: Windows" src="https://img.shields.io/badge/platform-Windows-0078D4.svg">
  <img alt="UI: WinUI 3" src="https://img.shields.io/badge/UI-WinUI%203-512BD4.svg">
  <img alt="Renderer: Direct2D" src="https://img.shields.io/badge/renderer-Direct2D-2F855A.svg">
</p>

---

Folia uses a WinUI 3 application shell and draws its document surface with
Direct2D and DirectWrite. It does not embed an HTML editor. Its editing state
combines one unified block tree with source-backed, lossless inline CSTs, so
structured editing does not require regenerating or normalizing Markdown that
the user did not change.

## Highlights

- **Source faithful** — preserves marker spelling, escapes, whitespace, link
  syntax, and temporarily incomplete editing states.
- **Native editing** — WYSIWYG and monospaced source modes, outline navigation,
  transactional history, and semantic copy/paste are implemented in native C++.
- **Rich rendering** — MathJax, Mermaid, Tree-sitter syntax highlighting,
  native SVG, animated GIFs, Windows image codecs (PNG, JPEG, BMP, TIFF, ICO,
  and installed WebP/HEIF codecs), tables, footnotes, callouts, and recursively
  nested blocks.
- **Focused reading** — configurable themes, English and Chinese interfaces,
  a borderless WinUI shell, and native Windows PDF export.
- **Clear boundaries** — the C++23 modules core remains platform independent;
  Windows, DirectX, and WinUI code stays in the platform and application layers.

## Architecture

```text
Markdown source
    └─ block tree
        └─ editable block
            ├─ exact inline source
            └─ lossless editable CST
                 ↓
        platform-neutral render model
                 ↓
        WinUI 3 + Direct2D/DirectWrite
```

See the [source layout](src/README.md), [core layout](src/core/README.md), and
[WinUI application layer](src/app-winui/README.md) for ownership boundaries.

## Build from source

Requirements: Windows 10 or 11, Visual Studio with the MSVC desktop workload,
CMake, Ninja, Rust/Cargo, and PowerShell.

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1
powershell -ExecutionPolicy Bypass -File .\build_app.ps1 -Configuration Debug
```

For a clean Release build:

```powershell
powershell -ExecutionPolicy Bypass -File .\build_app.ps1 -Configuration Release -Clean
```

Build the per-user x64 MSI with:

```powershell
powershell -ExecutionPolicy Bypass -File .\build_msi.ps1 -Version 0.1.0
```

The installer creates a self-contained Windows App SDK build, so the target
machine does not need a separately installed Windows App SDK runtime. The MSI
is written to `build/installer/bin/Folia-<version>-x64.msi`. It installs the
application below `%LOCALAPPDATA%\Programs\Folia` and the configurable Assets
tree below `%LOCALAPPDATA%\Folia\Assets`. Uninstall removes MSI-owned built-in
resources but preserves `settings.json` and `themes/custom/`.

For installer builds, `build_app.ps1` receives the compile-time Assets token
`{LocalAppData}\Folia\Assets`; it is resolved for the current user at runtime.
Normal developer builds continue to use their existing Assets location.

All generated files stay below `build/`. Application binaries are written to
`build/app-winui/bin/<platform>/<configuration>/`; builds do not write into
the source tree.

Run the core test suite with:

```powershell
cmd /c build_test.bat
```

The WinUI project entry point is [Folia.vcxproj](src/app-winui/Folia.vcxproj).
See the [theme documentation](docs/THEMES.md) for custom themes and external
Assets roots.

## Project status

Folia is under active development. The editor core, native renderer, and WinUI
application are functional; unit and randomized property tests continuously
exercise source losslessness and editing invariants.

## License

Folia is released under the [MIT License](LICENSE). Third-party licenses are
listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the complete
distribution notice referenced there.
