@echo off
rem Double-click to build the firmware locally. Output goes to the firmware folder.
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0local-build\build-local.ps1" %*
pause
