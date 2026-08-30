/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO user RGB LED (P1.22 red, P1.23 blue, P1.24 green) over PWM20.
 * Seeed's PWM sample fades one channel; cycling all three is the
 * "multicolor" rainbow the stock demos show.
 */
#pragma once

int status_led_init(void);
