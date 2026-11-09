@echo off
rem ============================================================
rem  Radeon Open KMD - one-click installer (test-signed driver)
rem  Right-click this file -> "Run as administrator"
rem ============================================================
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Administrator privileges required.
    echo         Right-click install.cmd and choose "Run as administrator".
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install.ps1"
echo.
pause
