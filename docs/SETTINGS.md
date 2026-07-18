# Settings

Settings is a full-window WinUI mode rather than a modal dialog. While it is active, the checked Settings command remains highlighted, document commands are hidden, and the document outline is replaced by General, Shortcuts, LaTeX Commands, Themes, Licenses, and About navigation. Press the checked Settings command again to return to the document without rebuilding the editor session.

## Math rendering

`Render mathematical formulas` owns the QuickJS MathJax service lifecycle. A persisted disabled setting leaves the service unstarted. Disabling it while the application is running cancels queued work, interrupts active QuickJS execution, joins the worker, releases its runtime, clears rendered math caches, and displays the Markdown math source. Enabling it creates a fresh worker and formulas are rendered on demand.

## Persistence

Settings use schema version 4 and are saved atomically to `Assets/settings.json`:

```json
{
  "schemaVersion": 4,
  "mathRenderingEnabled": true,
  "latexSuggestionsEnabled": true,
  "themeId": "system",
  "languageId": "system",
  "shortcutBindings": []
}
```

An invalid or unavailable file falls back to safe defaults and reports a diagnostic. The MathJax switch applies and saves immediately. Theme selection is staged: it updates only the theme preview until the Theme page's `Apply` button is pressed. Leaving Settings without applying discards the pending theme selection. The preview uses the profile's actual typography, code, quote, and color values. An active custom theme cannot be removed until a different theme has been applied, preventing a dangling persisted ID.

## Shortcuts and insertion actions

Shortcut bindings are resolved deterministically by editor context. A Code or Math binding overrides the same Global gesture only while the caret is in that context; conflicts inside one scope are rejected and reported before settings are saved. A binding with no gesture is intentionally disabled. There is no hidden hard-coded Ctrl-key fallback, so clearing a built-in binding actually disables it.

Custom insertion actions store multiline source templates and use a deterministic subset of VS Code snippet syntax:

- `$1`, `${1}`, and later numbered markers define the order visited by Tab; `$0` is the final caret stop.
- `${1:default}` inserts and selects default text, so typing replaces it before Tab advances.
- `${TM_SELECTED_TEXT}` inserts the exact selected block-local Markdown source.
- `${1:${TM_SELECTED_TEXT}}` inserts the selection as the first editable placeholder. A useful wrapping action is `\mathbf{${1:${TM_SELECTED_TEXT}}}$0`.
- `$$` inserts a literal dollar sign. Ordinary characters, including newlines, are preserved verbatim.

Insertion remains a normal block-local source edit, and the temporary Tab-stop session is UI state rather than a second document coordinate system. Selected source is inserted verbatim rather than parsed as another snippet, so Markdown and LaTeX dollar characters inside the selection are not accidentally interpreted as placeholders.

## LaTeX command suggestions

LaTeX suggestions have an independent immediate-apply switch. In rendered mode, typing a command prefix such as `\fr` inside inline or block math opens a native WinUI candidate popup beside the caret. Up/Down changes the selected candidate, Tab or Enter inserts it, and Escape closes the popup. Accepting a command replaces only the command prefix in the current editable block and reuses the same source-template and Tab-stop transaction path as custom shortcut insertions.

Built-in commands are loaded from `Assets/latex/commands.json`. Personal commands and usage history are stored separately in `Assets/latex/commands.user.json`, so application updates do not rewrite user data. Personal commands can shadow a built-in trigger without creating a compatibility entry. Usage ranking applies exponential time decay with a 14-day half-life; exact prefix matches remain first, and Settings can reset all usage scores. The personal command editor accepts multiline templates and uses the same snippet notation described above. Built-in matrix, cases, and aligned commands therefore insert real multiline environments; accepting a completion does not expose the command prefix itself as `TM_SELECTED_TEXT`.

## Built-in themes

Built-in themes are complete profiles rather than accent-color overlays. Each profile supplies editor and shell colors, typography, syntax colors, and block layout metrics.

- `Folia Light`, `Folia Dark`, and `Folia High Contrast` are the neutral defaults.
- `Inkstone Paper` is a spacious warm-paper reading theme with ink and cinnabar accents.
- `Moss & Linen` is a compact, low-glare light theme with subdued green structure.
- `Arctic Night` is a cool dark theme with cyan navigation and restrained syntax colors.
- `Ember Night` is a warm dark theme with amber headings and terracotta structure.
