/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * pH telemetry over Pouch Stream, plus Golioth Settings for interval,
 * temperature override, OEM LED, and calibration.
 *
 * Settings keys (create these in the Golioth console):
 *   SAMPLE_INTERVAL_S  int     seconds between gateway syncs (default 300)
 *   TEMP_OVERRIDE_C    float   compensation C if no DS18B20 (NaN/omit = auto)
 *   LED                bool    Atlas OEM LED
 *   CAL_CMD            int     0 idle, 1 clear, 2 low(4.0), 3 mid(7.0), 4 high(10.0)
 *                              Put the probe in that buffer, then set CAL_CMD.
 *                              Set back to 0 afterwards.
 */

#include "ph_oem.h"
#include "temp_sensor.h"
#include "battery.h"
#include "ble_peripheral.h"
#include "golioth_ph.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include <pouch/uplink.h>
#include <pouch/types.h>
#include <pouch/port.h>
#include <pouch/events.h>

#include <zephyr/sys/printk.h>

#include <golioth/settings_callbacks.h>

LOG_MODULE_REGISTER(golioth_ph);

#define UPLINK_BUF 192

static float temp_override_c;
static bool temp_override_set;
static int32_t last_cal_cmd;

static float compensation_temp_c(void)
{
    float t;

    if (temp_sensor_read_c(&t) == 0) {
        return t;
    }
    if (temp_override_set) {
        return temp_override_c;
    }
    return CONFIG_PH_DEFAULT_TEMP_C / 100.0f;
}

static int uplink_json(const char *path, const char *payload, int len)
{
    if (len <= 0 || len >= UPLINK_BUF) {
        return -EINVAL;
    }

    int err = pouch_uplink_entry_write(path,
                                       POUCH_CONTENT_TYPE_JSON,
                                       payload,
                                       (size_t)len,
                                       POUCH_FOREVER);
    if (err) {
        LOG_WRN("%s uplink failed (err %d)", path, err);
        printk("uplink %s failed (err %d)\n", path, err);
    } else {
        LOG_INF("%s %s", path, payload);
        printk("uplink %s %s\n", path, payload);
    }
    return err;
}

static void pouch_session_event(enum pouch_event event, void *ctx)
{
    ARG_UNUSED(ctx);

    switch (event) {
    case POUCH_EVENT_SESSION_START:
        printk("pouch: session start (gateway sync in progress)\n");
        LOG_INF("Pouch session start");
        break;
    case POUCH_EVENT_SESSION_END:
        printk("pouch: session end\n");
        LOG_INF("Pouch session end");
        break;
    default:
        printk("pouch: event %d\n", (int)event);
        break;
    }
}
POUCH_EVENT_HANDLER(pouch_session_event, NULL);

static void ph_uplink(void)
{
    printk("pouch: building .s/ph and .s/battery uplink\n");
    struct ph_oem_sample sample = {0};
    struct battery_sample bat = {0};
    const float temp_c = compensation_temp_c();
    char payload[UPLINK_BUF];
    int len;

    if (!ph_oem_is_ready()) {
        len = snprintf(payload, sizeof(payload),
                       "{\"error\":\"oem_missing\",\"temp_c\":%.2f}",
                       (double)temp_c);
    } else {
        int err = ph_oem_read(temp_c, &sample);
        if (err) {
            LOG_WRN("pH read failed (err %d)", err);
            len = snprintf(payload, sizeof(payload),
                           "{\"error\":\"read\",\"err\":%d,\"temp_c\":%.2f}",
                           err, (double)temp_c);
        } else {
            len = snprintf(payload, sizeof(payload),
                           "{\"ph\":%.3f,\"mv\":%.2f,\"temp_c\":%.2f,\"cal\":%u}",
                           (double)sample.ph,
                           (double)sample.mv,
                           (double)sample.temp_c,
                           sample.cal_bits);
        }
    }
    (void)uplink_json(".s/ph", payload, len);

    if (battery_is_ready() && battery_read(&bat) == 0) {
        len = snprintf(payload, sizeof(payload),
                       "{\"v\":%.3f,\"ma\":%.1f,\"temp_c\":%.1f,\"die_c\":%.1f,"
                       "\"vbus\":%s,\"chg\":\"%s\",\"chg_stat\":%u,\"err\":%u}",
                       (double)bat.voltage_v,
                       (double)bat.current_ma,
                       (double)bat.temp_c,
                       (double)bat.die_c,
                       bat.vbus ? "true" : "false",
                       bat.status,
                       bat.chg_stat,
                       bat.err);
    } else {
        len = snprintf(payload, sizeof(payload), "{\"error\":\"npm1300\"}");
    }
    (void)uplink_json(".s/battery", payload, len);
}

POUCH_UPLINK_HANDLER(ph_uplink);

static int sample_interval_cb(int32_t new_value)
{
    LOG_INF("Setting SAMPLE_INTERVAL_S -> %d", new_value);
    ble_peripheral_set_sync_period_s((uint32_t)new_value);
    return 0;
}
GOLIOTH_SETTINGS_HANDLER(SAMPLE_INTERVAL_S, sample_interval_cb);

static int temp_override_cb(double new_value)
{
    LOG_INF("Setting TEMP_OVERRIDE_C -> %g", new_value);
    temp_override_c = (float)new_value;
    temp_override_set = true;
    return 0;
}
GOLIOTH_SETTINGS_HANDLER(TEMP_OVERRIDE_C, temp_override_cb);

static int led_setting_cb(bool new_value)
{
    LOG_INF("Setting LED -> %d", (int)new_value);
    (void)ph_oem_set_led(new_value);
    return 0;
}
GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

static int cal_cmd_cb(int32_t new_value)
{
    if (new_value == last_cal_cmd) {
        return 0;
    }
    last_cal_cmd = new_value;

    if (new_value < 1 || new_value > 4) {
        return 0;
    }

    LOG_INF("Setting CAL_CMD -> %d", new_value);
    int err = ph_oem_calibrate((int)new_value);
    if (err) {
        LOG_ERR("Calibration failed (err %d)", err);
        return err;
    }
    return 0;
}
GOLIOTH_SETTINGS_HANDLER(CAL_CMD, cal_cmd_cb);

int golioth_ph_init(void)
{
    return 0;
}
