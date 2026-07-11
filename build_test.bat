@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\md-cpp
cmake -G Ninja -B build
if errorlevel 1 exit /b %errorlevel%
cmake --build build --target elmd_tests
if errorlevel 1 exit /b %errorlevel%
build\elmd_tests.exe
exit /b %errorlevel%
