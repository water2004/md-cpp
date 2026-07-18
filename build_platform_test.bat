@echo off
setlocal
for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL exit /b 1
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%ROOT%"
cmake -Wno-dev -G Ninja -S "%ROOT%" -B "%ROOT%\build\core"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%ROOT%\build\core" --target FoliaWindowsEditorTests
if errorlevel 1 exit /b %errorlevel%
"%ROOT%\build\core\FoliaWindowsEditorTests.exe" %*
exit /b %errorlevel%
