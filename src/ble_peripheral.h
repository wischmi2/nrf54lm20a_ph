/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void ble_peripheral_button_handler(void);
void ble_peripheral_request_gateway(bool request);
void ble_peripheral_set_sync_period_s(uint32_t seconds);
uint32_t ble_peripheral_get_sync_period_s(void);
int ble_peripheral_init(void);
/** Advertise with sync_req set. Stops after CONFIG_EXAMPLE_ADV_WINDOW_S if unused. */
int ble_peripheral_start(void);
void ble_peripheral_status_start(void);
/** Call when a Pouch session actually starts so a bounce will not trigger a long sleep. */
void ble_peripheral_mark_sync_ok(void);
