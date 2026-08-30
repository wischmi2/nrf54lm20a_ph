@echo off
rem Build for the XIAO nRF54LM20A app core, with MCUboot via sysbuild.
rem Prefer the existing Pouch v0.2.0 / NCS 3.4 workspace at C:\ncs\water-control-ws
rem (nRF Connect terminal, or `nrfutil toolchain-manager launch --ncs-version v3.4.0 --shell`).
if "%BOARD_ROOT%"=="" set BOARD_ROOT=C:/ncs/water-control-ws/board-root
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp --build-dir %~dp0build --sysbuild %~dp0 -- -DBOARD_ROOT=%BOARD_ROOT% %*
