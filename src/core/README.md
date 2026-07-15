# Core layout

`src/core` is platform independent and organized by responsibility:

- `support/`: fundamental types, UTF conversion, errors, diagnostics, settings, storage, scheduling, and instrumentation.
- `model/`: node ids, block tree, document state, inline CST/document, metadata, symbols, and extension data.
- `parsing/`: dialect rules, block and inline parsing, lossless serialization, block source handling, and slug generation.
- `editing/`: source edits, block-tree edits, input rules, history, selection, clipboard operations, commands, and transactions.
- `query/`: document text/symbol projections and outline construction.
- `rendering/`: platform-neutral render model, layout plan/tree, hit testing, selection geometry, and math/render descriptions.
- `source/`: source-mode editing and syntax-style projection.

Editing modules should expose narrow operations and keep dependency direction explicit. Block input is split into marker recognition, list transformations, non-list block transformations, and a small public coordinator. Document copy and paste are separate because selection slicing and semantic tree splicing have different invariants. Shared edit support is an aggregate over source-edit primitives, reversible block mutations, and invariant validation rather than one catch-all implementation file.

Some large files are intentionally cohesive:

- `parsing/parser.ixx` owns one block-parser state machine and its source-range accounting.
- `parsing/inline_parser.ixx` owns the lossless inline CST state machine.
- `rendering/render_builder.ixx` owns one recursive block-tree-to-render-model traversal.

Extract helpers from these only when they have an independent invariant and one-way dependency. Do not divide a parser state machine or tree traversal into arbitrary line ranges.
