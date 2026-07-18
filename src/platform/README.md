# Platform layout

`src/platform` contains narrow Windows adapters used below the WinUI application:

- `graphics/`: Direct2D factory and native theme adaptation.
- `text/`: DirectWrite factory and text measurement.
- `windows/editor/geometry/`: persistent block placement and viewport lookup.
- `windows/editor/layout/`: source/display mapping and deterministic viewport
  work planning.
- `windows/editor/interaction/`: DirectWrite hit testing plus deterministic
  scrolling, key-gesture, and selection-drag models.

This layer may use Windows, WinRT, Direct2D, and DirectWrite APIs. It must not own Markdown parsing, document mutation, selection, history, or application-shell behavior. Core modules must not depend on this directory.

Keep adapters small and capability-oriented. The editor modules accept plain
values and core `TextPosition`/`Command` types so `FoliaWindowsEditorTests` can
assert geometry, coordinates, caret movement, input translation, and viewport
planning without creating a WinUI window.

Application orchestration, XAML controls and routed events, swap-chain
submission, `DispatcherQueue` frame scheduling, media loading, and printing
belong in `app-winui`. They execute platform plans; they must not duplicate the
deterministic calculations in a second window-local path.
