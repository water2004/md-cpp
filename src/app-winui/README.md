# app-winui

WinUI 3/C++/WinRT application shell for Folia.

This layer owns the native window, command bar, collapsible outline/settings navigation, status/progress surfaces, file dialogs, and the `SwapChainPanel` host for the self-drawn editor surface. It must not implement Markdown mutations directly; UI actions are translated to core `Command` values and applied through core document transactions.

Source ownership:

- The directory root contains XAML application/window files and the composition root. `MainWindow.xaml.cpp` owns lifecycle and window state; command and event wiring live in separate translation units.
- `editor/session/` adapts the core editor/document state to the native
  application. The session facade is split by boundary: document/render-model
  lifecycle, rendered/source editing dispatch, search, and UTF-32-to-UTF-16
  text-input projection. These translation units share one session state; they
  must not introduce parallel selections or cached document models.
- `editor/interaction/` translates WinUI routed events into platform input
  actions and core commands; it also owns text input, scrolling, document
  actions, frame scheduling, and sidebar controllers. Long-running PDF export
  orchestration is isolated from file, clipboard, link, and image commands but
  remains part of the document controller lifecycle so detach/cancel has one
  owner.
- `editor/rendering/` owns Direct2D/DirectWrite materialization and drawing.
  `EditorSurfaceRenderer` is the stateful facade; document work is delegated to
  a preparation pass, a one-block resource preparer, and a render pass. Native
  layouts, COM resources, image handles, and the prepared-document cache stay
  here because they are application rendering state rather than portable
  policy.
- `media/` owns GIF decoding and MathJax, Mermaid, and SVG integration. MathJax
  runtime/queue ownership is separate from parsing the renderer's SVG fragment
  protocol; changing fragment measurement must not alter QuickJS lifecycle or
  scheduling. Images and animated GIFs are loaded asynchronously around the
  viewport; intrinsic metadata is retained so unloaded media does not move the
  surrounding layout.
- `settings/` owns WinUI settings pages and persisted application settings.
  Feature-specific schemas such as shortcut/snippet bindings serialize through
  dedicated components; the root settings store owns only schema versioning,
  validation dispatch, and atomic file replacement.
- `export/` owns Windows PDF/print integration and the value types used to
  report export progress across UI controllers.
- `localization/`, `theme/`, and `storage/` own compiled WinUI resources,
  complete theme materialization, the compile-time-configurable Assets root,
  and ICU-backed document byte decoding/encoding. Storage reports ranked
  detection candidates but does not own document state; the session owns the
  encoding selected for the currently open document.

The document pipeline has three app-owned stages:

1. `EditorDocumentPreparationPass` reconciles cached blocks and executes the
   viewport/prefetch/retention plans supplied by `folia_platform`.
2. `EditorDocumentBlockPreparer` turns one core `RenderBlock` into DWrite/D2D,
   code-highlight, math/SVG, Mermaid, and image resources without rewalking the
   document. Work is budgeted and prioritized around the viewport.
3. `EditorDocumentRenderPass` paints already-prepared visible blocks and
   populates the interaction map.

These are concrete collaborators, not virtual plug-ins. The hot path keeps the
same prepared cache, incremental work budget, and one visible-range traversal;
the split must not add per-block heap objects, callbacks, or a second document
scan. Large files remain appropriate for cohesive state machines such as GIF
decoding and PDF page recording. New UI commands and Markdown behavior do not
belong in rendering files.

Document opening and charset conversion are asynchronous at the shell boundary.
The status-bar encoding command separates detection-driven reopening (ranked
candidates with confidence) from explicit conversion saves (the alphabetical
ICU converter catalog without confidence). Picker selection,
command-line/Explorer activation, and MSI file associations all converge on
the same document controller; parsing still produces one authoritative core
document. PDF export records pages off the UI thread, reports preparation and
per-page progress, and observes cancellation through the document-controller
lifetime.

Settings replaces the document workspace rather than opening a separate
window. Its navigation currently contains General, Shortcuts, LaTeX Commands,
Themes, Licenses, and About. Only theme selection is staged behind an `Apply`
button; other switches and bindings persist immediately. See
[docs/SETTINGS.md](../../docs/SETTINGS.md) and
[docs/THEMES.md](../../docs/THEMES.md).

Do not create another top-level presentation library merely to move code. A
new shared presentation target is justified only when a second UI backend needs
the same platform-neutral presentation API. Until then, deterministic native
geometry and scheduling policy belongs in `platform`, while resource ownership
and orchestration remain in this application.

Build notes:

- Build `Folia.vcxproj` through the root `build_app.ps1` script so Windows App SDK XAML/IDL/C++WinRT generation runs through the NuGet targets and all output stays below `build/app-winui`.
- NuGet packages are restored into `packages/` by running `powershell -ExecutionPolicy Bypass -File setup.ps1` from the repository root.
- Keep CMake for `folia_core`, `folia_platform`, and tests; do not configure `-DFOLIA_BUILD_WINUI=ON`.
- Run `build_platform_test.bat` for deterministic Windows editor tests. These
  tests cover block geometry, DirectWrite hit testing/caret coordinates,
  table action/drop policy, preparation invalidation, viewport planning, scroll
  state, key gestures, and selection drag projection without UI automation.
- The editor area is a `SwapChainPanel`; the WinUI rendering layer consumes the core `RenderModel` and maps DirectWrite positions to block-local `TextPosition` values.
- Do not replace this with WebView2, RichEditBox, TextBox, RichTextBlock, or an HTML renderer.
