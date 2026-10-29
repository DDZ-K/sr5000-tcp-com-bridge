@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Run ONLY on an approved Windows x64 target PC, after installing a compatible signed com0com driver.
rem Usage: create_port_pair.bat COM18 COM19
if "%~1"=="" goto USAGE
if "%~2"=="" goto USAGE
set "PACKAGING=%~1"
set "BRIDGE=%~2"
if /I "%PACKAGING%"=="%BRIDGE%" (echo ERROR: Select two different free COM numbers.& exit /b 2)
set "SETUPC=%ProgramFiles%\com0com\setupc.exe"
if exist "%SETUPC%" goto FOUND
set "SETUPC=%ProgramFiles(x86)%\com0com\setupc.exe"
if exist "%SETUPC%" goto FOUND
echo ERROR: Install the signed com0com driver first; setupc.exe not found.
exit /b 2
:FOUND
echo.
echo Existing virtual pairs:
"%SETUPC%" list
echo.
echo Check Device Manager: %PACKAGING% and %BRIDGE% must be UNUSED.
echo This creates ONE NEW pair, it will not rename or replace any existing pair.
echo Packaging app receives on %PACKAGING%; bridge.ini BridgeCom must be %BRIDGE%.
echo To avoid old bytes after an application closes its COM port,
echo the receiving side enables EmuOverrun and sending side EmuBR.
choice /C YN /M "Are the two COM numbers free and is this the TEST PC"
if errorlevel 2 exit /b 1
"%SETUPC%" install PortName=%PACKAGING%,EmuOverrun=yes PortName=%BRIDGE%,dsr=ropen,EmuBR=yes
if errorlevel 1 (echo ERROR: com0com setup failed. Inspect Device Manager and setupc list.& exit /b 1)
echo.
"%SETUPC%" list
echo Confirm the new pair and driver status in Device Manager before production use.
echo Driver configuration is not proof against stale bytes: TEST close/reopen behavior.
exit /b 0
:USAGE
echo Run from an elevated Command Prompt on the target Windows x64 PC:
echo   create_port_pair.bat COM18 COM19
echo Replace these EXAMPLE COM numbers with two unused local numbers.
exit /b 2
