@echo off
setlocal
cd /d "%~dp0"
python backend\deploy\release.py %*
exit /b %ERRORLEVEL%
