@echo off
setlocal
set "CLI=%~dp0Ribanense.cli.ps1"
where pwsh >nul 2>&1
if %ERRORLEVEL%==0 (
  pwsh -NoProfile -ExecutionPolicy Bypass -File "%CLI%" %*
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%CLI%" %*
)
exit /b %ERRORLEVEL%
