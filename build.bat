@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1

cd /d "F:\diag_wake"

cl /EHsc /W4 /O2 /std:c++17 /nologo /utf-8 ^
  src\main.cpp ^
  src\monitor.cpp ^
  src\eventlog.cpp ^
  src\report.cpp ^
  /Fediag_wake.exe ^
  powrprof.lib wevtapi.lib ole32.lib oleaut32.lib

if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo Build succeeded: diag_wake.exe
