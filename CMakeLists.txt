# Copyright (c) 2026 Brian Wischmeyer
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Do not add a boards/<board>.overlay *and* rely on app.overlay: Zephyr's
# CMake stops after the boards overlay, so app.overlay is ignored. LittleFS
# fstab lives in boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay.

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_ph)

target_sources(app PRIVATE
  src/main.c
  src/credentials.c
  src/ble_peripheral.c
  src/fatal_error.c
  src/fw_update.c
  src/ph_oem.c
  src/temp_sensor.c
  src/battery.c
  src/status_led.c
  src/power_sleep.c
  src/golioth_ph.c
  src/ph_shell.c
)
