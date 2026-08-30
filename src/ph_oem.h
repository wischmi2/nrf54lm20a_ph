/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct ph_oem_sample {
    float ph;
    float mv;
    float temp_c;
    uint8_t cal_bits;
    bool valid;
};

/** Probe the OEM, leave it hibernating. Returns 0 if device-type is 0x01. */
int ph_oem_init(void);

bool ph_oem_is_ready(void);

int ph_oem_set_active(bool active);
int ph_oem_set_led(bool on);
int ph_oem_set_temp_c(float temp_c);

/**
 * Wake, wait for a conversion, read pH + mV + cal confirm, then hibernate.
 * Writes compensation temperature first if temp_c is finite.
 */
int ph_oem_read(float temp_c, struct ph_oem_sample *out);

int ph_oem_calibrate(int cmd);

int ph_oem_read_cal_confirm(uint8_t *bits);
int ph_oem_read_device_info(uint8_t *type, uint8_t *fw);
