@echo off
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp -p always --sysbuild %~dp0 %*
