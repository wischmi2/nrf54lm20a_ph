@echo off
cd /d "%~dp0"
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8
(
echo %DATE% %TIME%
smpmgr --port COM11 --line-length 127 --line-buffers 3 os reset
echo Exit code %ERRORLEVEL%
) > reset_log.txt 2>&1
