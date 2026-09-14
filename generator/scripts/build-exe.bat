@echo off
setlocal EnableExtensions

rem Keep one Windows build implementation. The batch helper forwards every
rem argument to the PowerShell script so runtime discovery and packaging stay
rem identical regardless of which Windows entry point is used.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-exe.ps1" %*
exit /b %ERRORLEVEL%
