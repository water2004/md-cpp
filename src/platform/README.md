# Platform layout

`src/platform` contains narrow Windows adapters used below the WinUI application:

- `graphics/`: Direct2D factory and native theme adaptation.
- `text/`: DirectWrite factory and text measurement.

This layer may use Windows, WinRT, Direct2D, and DirectWrite APIs. It must not own Markdown parsing, document mutation, selection, history, or application-shell behavior. Core modules must not depend on this directory.

Keep adapters small and capability-oriented. Application orchestration, controls, swap-chain drawing, media loading, and printing belong in `app-winui` instead.
