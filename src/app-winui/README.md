# app-winui

WinUI 3/C++/WinRT application shell for el-md.

This layer owns the native window, command bar, side panels, status bar, file dialogs, and the `SwapChainPanel` host for the self-drawn editor surface. It must not implement Markdown mutations directly; UI commands are translated to core `Command` values and applied through `editor-core` transactions.

Build notes:

- Build `el-md.vcxproj` with Visual Studio/MSBuild so Windows App SDK XAML/IDL/C++WinRT generation runs through the NuGet targets.
- NuGet packages are restored into `packages/` by running `powershell -ExecutionPolicy Bypass -File setup.ps1` from the repository root.
- Keep CMake for `elmd_core`, `elmd_platform`, and tests; do not configure `-DELMD_BUILD_WINUI=ON`.
- The editor area is `SwapChainPanel`; it is reserved for the Direct2D/DirectWrite renderer behind `elmd.platform.native_editor_surface`.
- Do not replace this with WebView2, RichEditBox, TextBox, RichTextBlock, or an HTML renderer.
