@echo off
rem Builds the native Harvest Moon on this PC and leaves HarvestMoonNative.exe here (see native\compilar.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0native\compilar.ps1"
echo.
pause
