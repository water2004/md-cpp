# Repository instructions

## Build and test

- Use C++23 modules and `import std;`.
- Core tests: `cmd /c build_test.bat 2>&1`.
- Core-only builds may target `elmd_core` with CMake.
- Restore WinUI/NuGet and native SVG dependencies with `powershell -ExecutionPolicy Bypass -File .\setup.ps1` before building the WinUI app.
- Keep generated output in ignored build directories; do not commit build products or restored packages.

## Code and platform boundaries

- Core modules under `src/core` are platform-independent and must not include Windows, WinRT, Direct2D, DirectWrite, DXGI, or TSF headers.
- Windows and rendering APIs belong in `src/platform` or the WinUI application layer.
- The application shell remains WinUI 3 with C++/WinRT; do not introduce a Win32 WndProc shell.
- Tests use the repository's forked Boost.UT C++23 module through `tests/elmd_test.hpp`, which performs `import boost.ut;`. Test translation units deliberately do not `import std;`; they include the shared test header and then import the required `elmd.core.*` modules. Do not replace this with the header-only `<boost/ut.hpp>` path. A single `tests/main.cpp` provides `main()` and passes `argc`/`argv` to `boost::ut::cfg<>.run()`; use a positional pattern such as `FoliaTests.exe "*name*"` to filter tests (`-t` lists tags).
- Prefer semantic fixes and focused tests. Do not add source-text special cases to make one example pass.

## Document model — block tree plus block-local lossless source

The authoritative editing state is a block-level structure tree. Each editable content node (Paragraph, Heading, TableCell, and the paragraph/heading bodies inside list items, task items, block quotes, callouts, and footnotes) owns the full, character-exact Markdown source of its inline content plus a lossless editable inline CST over that source.

- `InlineDocument.source` is the authoritative inline character content of its node: complete, unnormalized, including every marker, escape, entity, whitespace, and link-writing detail.
- `InlineDocument.tree` is a lossless editable CST over `source`. Concatenating its leaf token text in order must equal `source` exactly, character for character — not Markdown-semantically equal.
- The CST must preserve the original marker glyphs (`*` vs `_`, `**` vs `__`), escapes, entities, link/url spellings, quote forms, and whitespace, and must represent unclosed, ambiguous, and temporarily invalid editing states as error/incomplete nodes rather than collapsing them to plain text.
- Saving serializes the unchanged `source`; it must not re-derive or re-select markers from semantic fields for content the user did not edit.

## In-block coordinate system

Positions inside an editable content node are expressed as a container node id plus a source offset into that node's `InlineDocument.source`, with a text affinity. The offset counts every source character, including markers.

There is one in-block coordinate. Do not maintain parallel position schemes — logical text offsets, inline node ids, opening/content/closing marker-part offsets, or offsets into the serialized full document. `NodeId` may be returned by hit-testing or queries but is not an authoritative component of selection; CST nodes provide source ranges and structure only and do not define a second coordinate.

## Inline editing is source editing

All inline operations — character input, IME composition, Backspace, Delete, selection replacement, paste, drag-drop text, auto-pairing, bold, italic, strikethrough, inline code, links (insert/edit/remove), inline math — produce block-local source edits applied to the current node's `InlineDocument.source`. The flow is:

1. apply one or more `TextEdit`s to the current node's source;
2. update the selection's source offset;
3. re-parse the current node's inline CST only;
4. reconcile stable node ids where ranges are untouched or merely shifted;
5. update the affected layout and rendering.

Inline editing must not re-serialize the whole document, rebuild the full document AST, restore the cursor through a full-document source map, synchronize a legacy selection against the serialized document, or mutate semantic inline nodes directly (insert/erase into node text, split/merge inline vectors, reassign `children`). NodeId reconciliation is an optimization for keeping UI state on interactive links/code/math spans, not a precondition for cursor restoration.

## Block-level structure editing

Structure commands — split, join, insert, delete, set block type, indent/outdent list item, wrap/unwrap block quote, edit callout/footnote, edit table structure — modify the block tree. Enter inside a content node splits the node: cut the source at the current offset, create two nodes, parse each CST, move the caret to offset 0 of the second node. Cross-block Backspace/Delete joins nodes; it must not delete a newline in a serialized full-document string and re-parse. Keep block-text commands and block-structure commands as distinct layers; do not intermix them.

## Unified block tree

All structure nodes — document, block quote, list, list item, task list item, table, table row, table cell, callout, footnote, paragraph, heading, code block, math block, thematic break — share one tree accessed through uniform parent/children/insert/remove/replace/move/walk interfaces. The special side containers and their bespoke recursive entry points (`quote_children`, `list_items`, `task_items`, `table_header`, `table_rows`) are replaced by this single tree. Block-editing algorithms must not have separate hand-written recursions per node kind.

## Selection and history

Selection is container id plus source offset (and affinity) — one selection, no legacy/double-tracking. Undo/Redo records reversible transactions of `TextEdit`s and tree edits plus the selection before/after, and restores block state and selection exactly. It must not depend on re-parsing the full document. Complete snapshots may exist only as a debug/disaster fallback, never as the primary history mechanism for normal edits.

## Responsibilities

```text
Parser:       Markdown -> block tree + per-node InlineDocument (source + lossless CST)
Inline CST:   source -> token/CST (lossless, every character classified; one-node re-parse on edit)
Serializer:   block tree -> Markdown (inline = node.source, verbatim; markers never re-derived)
Editor:       TextEdit on node source -> re-parse one node -> reconcile ids -> render one node
Renderer:     node CST + selection -> visual representation; hit-test returns TextPosition
```

Markdown syntax recognition belongs to the inline CST parser, Markdown formatting belongs to the serializer, and keyboard interaction belongs to block-tree/source editing.

## Module boundaries and architectural discipline

Source editing, CST parsing, selection, and history remain separate, narrowly-scoped modules. `src/core/document_edit.ixx` is a thin facade and must not accumulate feature-specific editing logic. There is one source of truth: do not introduce an old/new dual track, a compatibility path, a feature flag selecting a prior model, or a fallback to legacy behavior. Reintroducing any of these is an architectural regression, not an incremental migration step.

## Invariants and testing

The CST must satisfy, continuously: `flatten_tokens(parse(source)) == source` and `serialize_lossless(parse(source)) == source`, both character-exact, preserving `*`/`_`, `**`/`__`, `[t](url)`/`[t](<url>)`, quote styles, escapes, and entities. Tests must exercise per-character input, deletion, selection, Enter, cross-block merge, format toggles, copy/paste, and undo/redo on representative inputs (`abc`, `*abc*`, `_abc_`, `**abc**`, `__abc__`, `**`, `**abc`, `a***b***c`, `~~abc~~`, `~~abc`, `` `abc` ``, `` `abc ``, `[title](url)`, `[title](<url>)`, `[title](url "name")`, `[title](`, `![alt](url)`, `$abc$`, `$abc`, `\*abc\*`, `&amp;`, `a\**b*`) across paragraph, heading, list/task item, quote, table cell, callout, footnote, and at document start/end/empty/adjacent-empty/nested contexts — checking source, CST flatten, block tree, selection, rendered text, save, reload, and undo/redo each time. Add random property tests asserting flatten == source, selection in range, save/reload losslessness, and exact undo/redo restoration. Normal-edit paths must perform zero full-document parses and zero full-document serializations; Unicode offsets must be one consistent unit across the core/render boundary, with explicit surrogate/combining/emoji conversion at the WinUI/DirectWrite edge.

## Agent workflow

Before changing editing interaction, understand the existing block tree, inline source/CST, document positions, transactions, normalization, selection, history, and serializer design. Treat the model in this document as the established architecture, not as an active migration. If implementation conflicts with these principles, call out the architectural regression and fix the responsible boundary when the task requires it. Do not add compatibility layers or revive legacy full-document/string-driven editing paths.

Keep changes scoped, preserve unrelated user work, and verify with the narrowest relevant tests plus a build when practical. Commit in small, reviewable batches — never one dump of hundreds of files. Do not use destructive Git operations unless the user explicitly authorizes them.
