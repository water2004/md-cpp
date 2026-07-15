# Settings

Settings is a full-window WinUI mode rather than a modal dialog. While it is active, the checked Settings command remains highlighted, document commands are hidden, and the document outline is replaced by General, Themes, and About navigation. Press the checked Settings command again to return to the document without rebuilding the editor session.

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

An invalid or unavailable file falls back to safe defaults and reports a diagnostic. Changes apply and save immediately. Selecting a theme updates the complete native shell and editor while the Settings view remains open, so the full application acts as the preview. Theme cards also preview the profile's actual typography, code, quote, and color values. Removing the active custom theme immediately repairs the selection to `system` instead of retaining a dangling ID.
