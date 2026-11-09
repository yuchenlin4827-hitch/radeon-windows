@echo off
rem ============================================================
rem  Radeon Open KMD - uninstaller
rem  Right-click this file -> "Run as administrator"
rem  Pass -OffTestsig to also turn testsigning off.
rem ============================================================
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Administrator privileges required.
    echo         Right-click uninstall.cmd and choose "Run as administrator".
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\uninstall.ps1" %*
echo.
pause
