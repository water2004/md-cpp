# Source layout

The source tree is divided by ownership rather than by file type:

- `core/` owns platform-independent Markdown state, parsing, editing, queries, and render descriptions.
- `platform/` adapts native graphics and text services without owning document behavior.
- `app-winui/` is the WinUI 3 composition root, interaction layer, and native renderer.

Dependencies point toward `core`. Core code must never include or import WinUI, WinRT, Direct2D, DirectWrite, DXGI, TSF, or other Windows APIs. Platform and application code may consume core modules, but Markdown mutations remain core commands and transactions.

```text
core (portable document semantics and render descriptions)
  ↑
platform (deterministic Windows geometry, input, and scheduling policy)
  ↑
app-winui (window, resources, asynchronous orchestration, and drawing)
```

The platform layer is intentionally not a second presentation framework. It
extracts calculations that benefit from deterministic tests without a window;
COM resource ownership and frame orchestration stay in `app-winui`. See each
directory's README for the exact boundary and [the test layout](../tests/README.md)
for how those boundaries are exercised.

Folders are physical ownership boundaries; C++ module names remain the API boundary. A file should move or split when its responsibilities cross ownership boundaries, not merely because it exceeds a line-count threshold.
