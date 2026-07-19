# Third-party notices

Folia includes and uses third-party software. The complete, version-specific
license notices distributed with the application are generated at:

`src/app-winui/Assets/licenses/THIRD-PARTY-NOTICES.txt`

The generated bundle is authoritative. It includes the repository's Boost.UT
test fork, the SRELL search engine, QuickJS and MathJax, Tree-sitter and its
language grammars, the native SVG/Mermaid Rust dependency graph, Windows App
SDK/NuGet packages, and bundled npm packages. Dependencies that are present
only because the Windows App SDK package graph restores them are listed too;
their presence does not imply that Folia embeds a WebView editor.

Regenerate the file after changing native, NuGet, Cargo, or npm dependencies:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\generate-third-party-notices.ps1
```

Folia itself is licensed under the MIT License in `LICENSE`.
