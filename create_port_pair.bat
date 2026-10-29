@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Arguments are intentionally ignored. Pair creation belongs to the validated native GUI.
if not exist "%~dp0SR5000Bridge-v2.1.exe" goto MISSING
start "" "%~dp0SR5000Bridge-v2.1.exe"
exit /b 0
:MISSING
echo ERROR: SR5000Bridge-v2.1.exe must be next to this launcher.
exit /b 2
