@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /std:c++14 /W4 /WX /DUNICODE /D_UNICODE /MT /O2 /Fe:SR5000TcpTester.exe tcp_tester.cpp /link /SUBSYSTEM:WINDOWS,6.01
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /EHsc /std:c++14 /W4 /WX /DUNICODE /D_UNICODE /DNDEBUG /MT /O2 /Fe:SR5000TcpTest.exe tcp_test.cpp /link /SUBSYSTEM:CONSOLE,6.01
if errorlevel 1 exit /b 1
SR5000TcpTest.exe
exit /b %errorlevel%
