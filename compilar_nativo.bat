@echo off
rem Compila la version nativa de Harvest Moon en esta PC (ver native\compilar.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0native\compilar.ps1"
echo.
pause
