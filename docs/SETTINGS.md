# Settings

Settings is a full-window WinUI mode rather than a modal dialog. While it is active, the checked Settings command remains highlighted, document commands are hidden, and the document outline is replaced by General, Shortcuts, Themes, Licenses, and About navigation. Press the checked Settings command again to return to the document without rebuilding the editor session.

## Math rendering

`Render mathematical formulas` owns the QuickJS MathJax service lifecycle. A persisted disabled setting leaves the service unstarted. Disabling it while the application is running cancels queued work, interrupts active QuickJS execution, joins the worker, releases its runtime, clears rendered math caches, and displays the Markdown math source. Enabling it creates a fresh worker and formulas are rendered on demand.

## Persistence

Settings use schema version 3 and are saved atomically to `Assets/settings.json`:

```json
{
  "schemaVersion": 3,
  "mathRenderingEnabled": true,
  "themeId": "system",
  "languageId": "system",
  "shortcutBindings": []
}
```

An invalid or unavailable file falls back to safe defaults and reports a diagnostic. The MathJax switch applies and saves immediately. Theme selection is staged: it updates only the theme preview until the Theme page's `Apply` button is pressed. Leaving Settings without applying discards the pending theme selection. The preview uses the profile's actual typography, code, quote, and color values. An active custom theme cannot be removed until a different theme has been applied, preventing a dangling persisted ID.

## Shortcuts and insertion actions

Shortcut bindings are resolved deterministically by editor context. A Code or Math binding overrides the same Global gesture only while the caret is in that context; conflicts inside one scope are rejected and reported before settings are saved. A binding with no gesture is intentionally disabled. There is no hidden hard-coded Ctrl-key fallback, so clearing a built-in binding actually disables it.

Custom insertion actions store source templates. `$1`, `$2`, and later numbered markers define the order visited by Tab, while `$0` is the final caret stop. `$$` inserts a literal dollar sign. Insertion remains a normal block-local source edit, and the temporary Tab-stop session is UI state rather than a second document coordinate system.

## Built-in themes

Built-in themes are complete profiles rather than accent-color overlays. Each profile supplies editor and shell colors, typography, syntax colors, and block layout metrics.

- `Folia Light`, `Folia Dark`, and `Folia High Contrast` are the neutral defaults.
- `Inkstone Paper` is a spacious warm-paper reading theme with ink and cinnabar accents.
- `Moss & Linen` is a compact, low-glare light theme with subdued green structure.
- `Arctic Night` is a cool dark theme with cyan navigation and restrained syntax colors.
- `Ember Night` is a warm dark theme with amber headings and terracotta structure.
