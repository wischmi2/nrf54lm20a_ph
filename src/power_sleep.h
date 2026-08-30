/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drop always-on loads between Pouch syncs. Radio stop/start lives in
 * ble_peripheral; this module handles Atlas, RGB, local sampling, and I2C.
 */
#pragma once

/** Hibernate Atlas, LEDs off, pause local ph log, suspend TWIM22. */
void power_sleep_enter(void);

/** Resume TWIM22 and local sampling before the next advertise/read. */
void power_sleep_exit(void);

/** Bring I2C back if a shell/read path runs during the sleep window. */
void power_sleep_buses_resume(void);
