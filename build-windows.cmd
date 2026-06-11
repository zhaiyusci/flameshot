@echo off
setlocal
cd /d "%~dp0"

call "%~dp0scripts\build-windows-release.cmd" %*
exit /b %ERRORLEVEL%
