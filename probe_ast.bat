@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\md-cpp
if not exist build mkdir build
cl /std:c++latest /utf-8 /nologo /c /ifcOutput build /ifcSearchDir build src\core\ast.ixx