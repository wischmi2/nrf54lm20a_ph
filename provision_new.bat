@echo off
rem Load the Oct 2026 Golioth cert + key onto the XIAO over COM11.
cd /d "%~dp0"
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8
(
echo %DATE% %TIME%
where smpmgr
set SERIAL_PORT=COM11
set CERT_FILE=ph_nrf54lm20a.crt.der
set KEY_FILE=ph_nrf54lm20a.key.der
call provision.bat
echo Exit code %ERRORLEVEL%
) > provision_log.txt 2>&1
type provision_log.txt
pause
