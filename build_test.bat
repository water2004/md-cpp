@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\md-cpp
cmake -G Ninja -B build 2>&1 | findstr /V "experimental CxxImportStd"
cmake --build build --target elmd_tests 2>&1 | findstr /V "C5201 experimental"
build\elmd_tests.exe