@echo off
REM ARMOR-RADAR runtime launcher. GPL-3.0-or-later.
call "%~dp0..\ARMOR-COMMON\scripts\armor-project.bat" run "%~dp0."
exit /b %ERRORLEVEL%
