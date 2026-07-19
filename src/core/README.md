# Core layout

`src/core` is platform independent and organized by responsibility:

- `support/`: fundamental types, UTF conversion, errors, diagnostics, settings, storage, scheduling, image metadata, export descriptions, and instrumentation.
- `model/`: node ids, block tree, document state, inline CST/document, metadata, symbols, and extension data.
- `parsing/`: dialect rules, block/inline/HTML CST parsing, lossless serialization, block source handling, and slug generation.
- `editing/`: block-local source edits, block-tree edits, input rules, history, selection, semantic clipboard splicing, snippets, commands, and transactions.
- `query/`: document content/context projections, outline construction, rendered/source search, SRELL regular expressions, and LaTeX catalog/completion queries.
- `rendering/`: platform-neutral render model, layout plan/tree, hit testing, selection geometry, HTML presentation, and math/render descriptions.
- `source/`: source-mode editing, line projection, and syntax-style projection.

Editing modules should expose narrow operations and keep dependency direction explicit. Block input is split into marker recognition, list transformations, non-list block transformations, and a small public coordinator. Document copy and paste are separate because selection slicing and semantic tree splicing have different invariants. Shared edit support is an aggregate over source-edit primitives, reversible block mutations, and invariant validation rather than one catch-all implementation file.

`document_edit.ixx` is a thin facade over feature-specific modules. Inline input,
formatting, snippets, and search replacement produce block-local `TextEdit`s;
Enter, cross-block deletion, list/quote/callout/footnote behavior, and table
operations produce block-tree transactions. Neither path may serialize and
reparse the full document to restore selection.

Inline formatting is a CST query followed by one block-local source edit. Existing formatting is identified from complete CST delimiter ranges, so removing `_`/`__`, variable-length code spans, math, and nested formatting preserves the exact source spelling instead of comparing against hard-coded marker text.

Callout titles are ordinary `CalloutTitle` block nodes: when present they are the first child of the `Callout`, own their own `InlineDocument`, source coordinate, and node identity, and participate in the same recursive traversal, clipboard, history, rendering, and validation paths as every other editable child. The Callout container owns only structural metadata; it has no editable side document.

Some large files are intentionally cohesive:

- `parsing/parser.ixx` owns one block-parser state machine and its source-range accounting.
- `parsing/inline_parser.ixx` owns the lossless inline CST state machine.
- `rendering/render_builder.ixx` owns one recursive block-tree-to-render-model traversal.

HTML remains a lossless syntax island in the document model. The rendering
layer derives only allow-listed presentation: `html_inline_presentation.ixx`
projects safe inline styles, while `html_inline_flow.ixx` applies HTML
formatting-context whitespace rules without changing source text or source
spans. Block-level alignment is carried explicitly through `BlockStyle`; it is
not inferred from rendered coordinates.

Rendered search uses the visible-text projection and maps matches back to
block-local source ranges, so Markdown markers and paired HTML tags are not
searchable decoration. Source-mode search operates on the exact Markdown
buffer. Both modes use the same SRELL-backed query engine and commit
replacements through the appropriate source/tree editing boundary.

Extract helpers from these only when they have an independent invariant and one-way dependency. Do not divide a parser state machine or tree traversal into arbitrary line ranges.

Run the portable suite with `cmd /c build_test.bat`; test ownership and
filtering are documented in [tests/README.md](../../tests/README.md).
