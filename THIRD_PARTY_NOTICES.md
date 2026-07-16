# Third-party notices

Folia includes and uses third-party software. The complete, version-specific
license notices distributed with the application are generated at:

`src/app-winui/Assets/licenses/THIRD-PARTY-NOTICES.txt`

Regenerate the file after changing native, NuGet, Cargo, or npm dependencies:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\generate-third-party-notices.ps1
```

Folia itself is licensed under the MIT License in `LICENSE`.
