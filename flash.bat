@echo off
rem Flash MCUboot + signed app to the XIAO nRF54LM20A over onboard CMSIS-DAP.
rem OpenOCD/nrfutil cannot program this board on Windows (no J-Link; WinUSB
rem CMSIS-DAP v2 has no DeviceInterfaceGUIDs). probe-rs uses CMSIS-DAP HID.
setlocal
set PR=%~dp0tools\probe-rs\probe-rs.exe
set HEX=%~dp0build\merged.hex
set BOOT=%~dp0build\mcuboot\zephyr\zephyr.hex
set APP=%~dp0build\xiao_ph\zephyr\zephyr.signed.hex
if not exist "%PR%" (
  echo Missing probe-rs at %PR%
  exit /b 1
)
if not exist "%BOOT%" (
  echo Missing MCUboot hex. Build first.
  exit /b 1
)
if not exist "%APP%" (
  echo Missing signed app hex. Build first.
  exit /b 1
)
"C:\Program Files\Nordic Semiconductor\nrf-command-line-tools\bin\mergehex.exe" -m "%BOOT%" "%APP%" -o "%HEX%"
if errorlevel 1 exit /b 1
"%PR%" download --chip nRF54LM20A --probe 2886:0068 --binary-format hex --verify --non-interactive "%HEX%"
if errorlevel 1 exit /b 1
"%PR%" reset --chip nRF54LM20A --probe 2886:0068 --non-interactive
