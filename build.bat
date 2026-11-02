@echo off
rem Builds the native Harvest Moon on this PC and leaves HarvestMoonNative.exe here (see native\build.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0native\build.ps1"
echo.
pause
