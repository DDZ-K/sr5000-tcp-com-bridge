@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul
if errorlevel 1 (
  echo Run this from the Visual Studio x64 Native Tools Command Prompt.
  exit /b 1
)
cl /nologo /utf-8 /EHsc /W4 /DUNICODE /D_UNICODE /MT /O2 /Fe:SR5000Bridge-v2.exe bridge.cpp /link /SUBSYSTEM:WINDOWS,6.01
if errorlevel 1 exit /b 1
echo Built SR5000Bridge-v2.exe
