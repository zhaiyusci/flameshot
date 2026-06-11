@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."

cd /d "%REPO_ROOT%"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%windows-build.ps1" %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

echo.
if "%BUILD_EXIT_CODE%"=="0" (
  echo Windows release build finished successfully.
) else (
  echo Windows release build failed with exit code %BUILD_EXIT_CODE%.
)
echo.
if /i "%BUILD_WINDOWS_NO_PAUSE%"=="1" exit /b %BUILD_EXIT_CODE%
pause
exit /b %BUILD_EXIT_CODE%
