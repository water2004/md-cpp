# Settings

The WinUI Settings dialog contains General, Themes, and About pages.

## Math rendering

`Render mathematical formulas` owns the QuickJS MathJax service lifecycle. A persisted disabled setting leaves the service unstarted. Disabling it while the application is running cancels queued work, interrupts active QuickJS execution, joins the worker, releases its runtime, clears rendered math caches, and displays the Markdown math source. Enabling it creates a fresh worker and formulas are rendered on demand.

## Persistence

Settings use schema version 1 and are saved atomically to `Assets/settings.json`:

```json
{
  "schemaVersion": 1,
  "mathRenderingEnabled": true,
  "themeId": "system"
}
```

An invalid or unavailable file falls back to safe defaults and reports a diagnostic in the status bar. Theme import and removal are file-management operations and therefore happen immediately. The selected theme and math setting are applied only after Save. If the active custom theme is removed and the dialog is cancelled, the application repairs the selection to `system` instead of retaining a dangling ID.
