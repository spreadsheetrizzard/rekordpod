@echo off
setlocal
title Rekordpod Installer Builder

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0build_windows.ps1" "%~dp0assets" "%~dp0dist-release"

if errorlevel 1 (
  echo.
  echo The Rekordpod Installer build did not finish.
  pause
  exit /b 1
)

echo.
echo Finished. Open the dist-release folder for Rekordpod Installer.exe.
pause
