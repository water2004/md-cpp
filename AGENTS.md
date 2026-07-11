# Repository instructions

## Build and test

- Use C++23 modules and `import std;`.
- Core tests: `cmd /c build_test.bat 2>&1`.
- Core-only builds may target `elmd_core` with CMake.
- Restore WinUI/NuGet and native SVG dependencies with `powershell -ExecutionPolicy Bypass -File .\setup.ps1` before building the WinUI app.
- Keep generated output in ignored build directories; do not commit build products or restored packages.

## Code and platform boundaries

- Core modules under `src/core` are platform-independent and must not include Windows, WinRT, Direct2D, DirectWrite, DXGI, or TSF headers.
- Windows and rendering APIs belong in `src/platform` or the WinUI application layer.
- The application shell remains WinUI 3 with C++/WinRT; do not introduce a Win32 WndProc shell.
- Tests include `tests/test_framework.h` after `import std;`; test macros do not cross module boundaries.
- Prefer semantic fixes and focused tests. Do not add source-text special cases to make one example pass.

## Document model

`EditorDocument` and its parsed Markdown AST are the only authoritative editing state after parsing completes.

Markdown source text is only used as:

- parser input when opening or reloading a file;
- serializer output when saving, exporting, or copying Markdown;
- a disposable or regenerable derived cache when necessary.

Markdown text, `TextBuffer`, SourceMap, RenderModel, and LayoutTree must not be described or used as parallel authoritative state.

The responsibilities are:

```text
Parser: Markdown -> EditorDocument
Serializer: EditorDocument -> Markdown
Editor commands: EditorDocument -> EditorDocument
Renderer: EditorDocument -> visual representation
```

Rendering, layout, source mapping, outline, and diagnostics are derived state.

## Editing interaction

Normal editing commands operate on AST nodes and document positions through structured tree transformations. Enter, Backspace, Delete, Tab, Shift+Tab, paste, list indentation, quote operations, block split/merge, and range deletion must not scan Markdown line prefixes or assemble temporary Markdown strings.

Markdown syntax recognition belongs to the parser, Markdown formatting belongs to the serializer, and keyboard interaction belongs to document-tree editing.

## Selection and history

Selections should bind stable node identity plus an offset within that node, rather than depending only on a global serialized-text offset.

Undo/Redo restores both document state and selection. Character-level source replacement may remain as a compatibility or serialization layer, but it must not define command semantics.

## Agent workflow

Before changing editing interaction, understand the existing AST, document positions, transactions, normalization, selection, history, and serializer design. If implementation conflicts with these principles, call out the architectural debt and fix the responsible document-tree boundary when the task requires it. Do not begin a broad migration unless the task explicitly asks for one, and do not expand legacy source-driven editing paths.

Keep changes scoped, preserve unrelated user work, and verify with the narrowest relevant tests plus a build when practical. Do not use destructive Git operations unless the user explicitly authorizes them.
