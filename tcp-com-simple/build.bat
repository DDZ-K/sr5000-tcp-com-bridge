@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
rc /nologo /fo operator.res operator.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /std:c++14 /W4 /WX /DUNICODE /D_UNICODE /MT /O2 /Fe:SR5000TcpComBridge.exe scanner_bridge.cpp /link /SUBSYSTEM:WINDOWS,6.01 /MANIFEST:NO operator.res
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /std:c++14 /W4 /WX /DUNICODE /D_UNICODE /DNDEBUG /MT /O2 /Fe:ScannerBridgeTest.exe scanner_bridge_test.cpp /link /SUBSYSTEM:CONSOLE,6.01 /MANIFEST:NO operator.res
if errorlevel 1 exit /b 1
ScannerBridgeTest.exe
exit /b %errorlevel%
