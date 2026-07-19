# Platform layout

`src/platform` contains narrow Windows adapters used below the WinUI application:

- `graphics/`: Direct2D factory and native theme adaptation.
- `text/`: DirectWrite factory and text measurement.
- `windows/editor/geometry/`: persistent block placement and viewport lookup.
- `windows/editor/layout/`: source/display mapping, preparation invalidation,
  priority work queues, and deterministic viewport/prefetch/retention work
  planning.
- `windows/editor/interaction/`: DirectWrite hit testing plus deterministic
  input-command translation, table-action/drop hit policy, scrolling,
  key-gesture/shortcut settings, snippet/LaTeX completion sessions, and
  selection-drag models.

This layer may use Windows, WinRT, Direct2D, and DirectWrite APIs. It must not own Markdown parsing, document mutation, selection, history, or application-shell behavior. Core modules must not depend on this directory.

Keep adapters small and capability-oriented. The editor modules accept plain
values and core `TextPosition`/`Command` types so `FoliaWindowsEditorTests` can
assert geometry, coordinates, caret movement, input translation, and viewport
planning without creating a WinUI window.

Application orchestration, XAML controls and routed events, swap-chain
submission, `DispatcherQueue` frame scheduling, media loading, and printing
belong in `app-winui`. They execute platform plans; they must not duplicate the
deterministic calculations in a second window-local path.

Platform APIs return values and plans; they do not own application resources.
For example, the table interaction module decides which semantic table action
a coordinate represents, while the WinUI application paints the D2D controls.
Likewise, the preparation planner decides which block indices are invalidated
or retained, while the application creates and releases DWrite/image resources.

Run these deterministic Windows tests with `cmd /c build_platform_test.bat`.
They are not WinUI automation: XAML controls, window activation, swap-chain
submission, and asynchronous media lifetimes are verified by application
build/manual integration checks instead.
