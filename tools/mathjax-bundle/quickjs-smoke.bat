@echo off
setlocal
set "ROOT=%~dp0..\.."
set "OUTPUT=%ROOT%\build\tools\mathjax-bundle"
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSINSTALL=%%i
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
pushd "%~dp0"
cl /nologo /MD /DNDEBUG /std:c++20 /EHsc quickjs-smoke.cpp /Fo:"%OUTPUT%\quickjs-smoke.obj" /Fe:"%OUTPUT%\quickjs-smoke.exe" "%ROOT%\build\app-winui\obj\x64\Release\quickjs\quickjs.lib" d2d1.lib d3d11.lib dxgi.lib ole32.lib /link /STACK:33554432
if errorlevel 1 exit /b %errorlevel%
"%OUTPUT%\quickjs-smoke.exe"
popd
