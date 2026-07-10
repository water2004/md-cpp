@echo off
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSINSTALL=%%i
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
cl /nologo /MD /DNDEBUG /std:c++20 /EHsc quickjs-smoke.cpp /Fe:quickjs-smoke.exe ..\..\src\app-winui\el-md\x64\Release\quickjs\quickjs.lib /link /STACK:33554432
if errorlevel 1 exit /b %errorlevel%
quickjs-smoke.exe
