@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
where cl.exe >nul 2>nul
if errorlevel 1 (
  echo Run this from a Visual Studio x64 Native Tools Command Prompt.
  exit /b 1
)
cl /nologo /utf-8 /EHsc /std:c++14 /W4 /WX /DUNICODE /D_UNICODE /MT /O2 /Fe:SR5000Bridge-v2.1.exe bridge.cpp /link /SUBSYSTEM:WINDOWS,6.01
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /std:c++14 /W4 /WX /DUNICODE /D_UNICODE /DNDEBUG /MT /O2 /Fe:bridge_review_test.exe test.cpp /link /SUBSYSTEM:CONSOLE,6.01
if errorlevel 1 exit /b 1
bridge_review_test.exe
if errorlevel 1 exit /b 1
echo Built and regression-tested SR5000Bridge-v2.1.exe
exit /b 0
