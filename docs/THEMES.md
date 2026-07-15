# Theme configuration

The application loads built-in themes from `Assets/themes` and manages imported themes in `Assets/themes/custom`. The repository ships complete `dark.json`, `light.json`, and `high-contrast.json` profiles; MSBuild copies them to the configured Assets directory. Imported profiles can be previewed, selected, replaced by ID, and removed from the native full-window Settings view. Built-in profiles cannot be removed.

The theme file is the normal runtime source of truth for:

- editor and shell colors, including selection, caret, Markdown markers, code, math, tables, quotes, callouts, and syntax highlighting;
- body, heading, code, UI, and UI-monospace fonts, sizes, line heights, weights, and italic style;
- document padding and block spacing;
- quote, callout, code, math, table, image, TOC, frontmatter, footnote, and unsupported-block geometry;
- title bar, navigation pane, scrollbar, status bar, and footnote-preview dimensions.

Every property in the shipped files is required. A file with a missing field, wrong type, invalid color, unsupported schema version, or mismatched variant is rejected as a whole. The application reports the failure in the status bar and uses one complete built-in fail-safe profile; it never combines a partially loaded file with unrelated hardcoded values.

Colors use `#RRGGBB` or `#RRGGBBAA`, with alpha in the final byte. Font weights use numeric OpenType/DirectWrite values from 100 through 999. All sizes and layout values are device-independent pixels and must be finite and non-negative.

The `syntax` array has exactly eleven entries in this order:

1. plain text
2. keyword
3. type
4. function
5. string
6. number
7. comment
8. punctuation/operator
9. preprocessor
10. variable/property
11. constant/special

Theme selection in Settings changes only the preview until `Apply` is pressed. Applying a theme rebuilds both the block render model and native DirectWrite/Direct2D resources. The applied `Follow Windows` selection reloads the matching light, dark, or high-contrast profile when the Windows theme changes.

## Assets directory

All application data and resource paths are relative to one Assets root:

- `settings.json` stores the selected theme and MathJax setting;
- `themes/*.json` contains protected built-in profiles;
- `themes/custom/*.json` contains user-managed profiles;
- `mathjax/` contains the QuickJS MathJax bundle and its font modules.

Without an explicit build value, the root is `./Assets` relative to the process working directory. Set the MSBuild property `ElMdAssetsDirectory` to name the Assets directory itself:

```powershell
msbuild src\app-winui\el-md.vcxproj /p:Configuration=Release /p:Platform=x64 /p:ElMdAssetsDirectory="C:\path with spaces\Assets"
```

The configured value is compiled into the application and static Assets are copied there after the build. Environment references such as `%LOCALAPPDATA%` are expanded by the application at runtime; packaging should pass or materialize an appropriate per-user Assets path rather than writing settings into the installed program directory.
