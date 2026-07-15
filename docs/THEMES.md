# Theme configuration

The application loads its active theme from `Assets/themes` beside the executable. The repository ships complete `dark.json`, `light.json`, and `high-contrast.json` profiles; MSBuild copies them to the output directory.

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

To customize a profile, edit the corresponding JSON file and restart the application. Theme changes caused by the Windows light/dark or high-contrast setting reload the matching profile and rebuild both the block render model and native DirectWrite/Direct2D resources.
