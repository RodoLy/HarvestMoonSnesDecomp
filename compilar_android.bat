@echo off
rem Builds the Android version (HarvestMoon.apk) on this PC (see android\compilar_android.ps1).
rem Compila la version de Android (HarvestMoon.apk) en esta PC.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0android\compilar_android.ps1"
echo.
pause
