/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * nPM1300 charger measurements (same path as Seeed's XIAO battery sample).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct battery_sample {
    float voltage_v;
    float current_ma;
    float temp_c;
    float die_c;
    bool vbus;
    const char *status;
    uint8_t chg_stat;
    uint8_t err;
    bool valid;
};

int battery_init(void);

bool battery_is_ready(void);

int battery_read(struct battery_sample *out);
