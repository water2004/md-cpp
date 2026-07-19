# Test layout

Folia has two Boost.UT C++23-module test executables:

- `FoliaTests` covers the platform-independent core under `foundation/`,
  `parsing/`, `editing/`, `query/`, and `rendering/`.
- `FoliaWindowsEditorTests` covers deterministic Windows editor adapters under
  `platform/` without creating a WinUI window.

Run them from a Visual Studio developer environment through the repository
wrappers:

```powershell
cmd /c build_test.bat
cmd /c build_platform_test.bat
```

Both wrappers configure `build/core`, build only the requested executable, and
forward command-line arguments. Boost.UT accepts a positional name pattern,
for example `cmd /c build_test.bat "*inline*"`; `-t` lists tags.

## Conventions

- Every test translation unit includes `support/folia_test.hpp` first. That
  shared header imports the repository's forked `boost.ut` module and exposes
  the common literals; test files deliberately do not `import std;`.
- Import the narrowest required `folia.core.*` or `folia.platform.*` modules.
  Do not use the header-only `<boost/ut.hpp>` path.
- Shared fixtures belong in `support/`; feature tests stay beside the owning
  concern rather than accumulating in a single giant file.
- Core tests must remain platform independent. Window/XAML behavior is not
  simulated inside the core suite; deterministic geometry, scrolling,
  shortcuts, snippet sessions, completion sessions, and viewport work plans
  belong in the platform suite.

Editing and parsing tests assert the long-lived architectural invariants, not
only visible examples: inline CST flattening and serialization are
character-exact, selection remains in one block-local source coordinate,
normal edits avoid full-document parse/serialization, and Undo/Redo restores
the exact tree, source, and selection. Property tests exercise randomized
source edits, nested containers, structural transactions, tables, save/reload,
and malformed-but-editable Markdown states.
