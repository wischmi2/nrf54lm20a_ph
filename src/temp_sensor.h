/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>

/** Probe the optional DS18B20 on D0. Missing sensor is not fatal. */
int temp_sensor_init(void);

bool temp_sensor_is_ready(void);

/**
 * Read temperature in Celsius.
 * @return 0 on success, negative errno if no sensor or conversion failed.
 */
int temp_sensor_read_c(float *temp_c);
