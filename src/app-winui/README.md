# app-winui

WinUI 3/C++/WinRT application shell for Folia.

This layer owns the native window, command bar, side panels, status bar, file dialogs, and the `SwapChainPanel` host for the self-drawn editor surface. It must not implement Markdown mutations directly; UI commands are translated to core `Command` values and applied through `editor-core` transactions.

Source ownership:

- The directory root contains XAML application/window files and the composition root. `MainWindow.xaml.cpp` owns lifecycle and window state; command and event wiring live in separate translation units.
- `editor/session/` adapts the core editor/document state to the native application.
- `editor/interaction/` owns pointer, keyboard, text input, scrolling, document actions, and sidebar controllers.
- `editor/rendering/` owns Direct2D/DirectWrite preparation and drawing. Lifecycle, image, text-layout, and SVG caches are separate translation units.
- `media/` owns GIF decoding and MathJax, Mermaid, and SVG integration.
- `export/` owns Windows PDF/print integration.

Large rendering files are acceptable when they form one pipeline or state machine. In particular, the document renderer remains a coupled prepare/layout/draw traversal, and GIF decoding keeps its shared canvas, disposal, and worker state together. New UI commands and Markdown behavior should not be added to those files.

Build notes:

- Build `Folia.vcxproj` through the root `build_app.ps1` script so Windows App SDK XAML/IDL/C++WinRT generation runs through the NuGet targets and all output stays below `build/app-winui`.
- NuGet packages are restored into `packages/` by running `powershell -ExecutionPolicy Bypass -File setup.ps1` from the repository root.
- Keep CMake for `elmd_core`, `elmd_platform`, and tests; do not configure `-DFOLIA_BUILD_WINUI=ON`.
- The editor area is a `SwapChainPanel`; the WinUI rendering layer consumes the core `RenderModel` and maps DirectWrite positions to block-local `TextPosition` values.
- Do not replace this with WebView2, RichEditBox, TextBox, RichTextBlock, or an HTML renderer.
