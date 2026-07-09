# AGENTS.md — opencode session guidance for D:\md-cpp

## Build / test commands
- Core lib + all 99 tests: `cmd /c build_test.bat 2>&1` (uses vcvars64 + Ninja).
- Core lib only: `cmake --build build --target elmd_core` (run in `D:\md-cpp`).
- WinUI 3 app: configure with `-DELMD_BUILD_WINUI=ON` (needs Windows App SDK + C++/WinRT headers).

## Project conventions
- C++23 with C++ modules (`.ixx` module interface units) + `import std;`.
- Core (`src/core/*.ixx`) is pure C++: NEVER include Windows headers or import windows.
- Platform layer (`src/platform/*`) is the ONLY place Windows/DirectWrite/D2D/DXGI/TSF headers may appear.
- App shell MUST be WinUI 3 + C++/WinRT (`src/app-winui/*.cpp + *.xaml`). Do NOT create a Win32 WndProc shell.
- Tests: `tests/*.cpp` use header-only `tests/test_framework.h`; each TU does `import std;` then `#include "test_framework.h"` then `import elmd.core.*;`. Macros cannot cross modules.
- Module names: avoid C++ keywords; use `elmd.core.exporter` not `elmd.core.export`.
- No comments unless requested.

## Key references
- Full spec + migration status: `D:\md-cpp\MIGRATION.md` (read this first).
- Original Rust project: `D:\el-md` (`project.txt` = spec, `HANDOFF.md` = migration notes).