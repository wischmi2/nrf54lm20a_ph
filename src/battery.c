/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * nPM1300 VBAT / IBAT / NTC / VBUS via the onboard gpio-i2c PMIC bus
 * (P1.18 SDA, P1.17 SCL), matching Seeed's zephyr-battery sample.
 */

#include "battery.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(battery, CONFIG_LOG_DEFAULT_LEVEL);

#define CHARGER_NODE DT_NODELABEL(pmic_charger)

#if !DT_NODE_HAS_STATUS(CHARGER_NODE, okay)
#error "nPM1300 charger node pmic_charger is missing from the board DTS"
#endif

#define NPM13XX_CHG_STATUS_COMPLETE_MASK  BIT(1)
#define NPM13XX_CHG_STATUS_TRICKLE_MASK   BIT(2)
#define NPM13XX_CHG_STATUS_CC_MASK        BIT(3)
#define NPM13XX_CHG_STATUS_CV_MASK        BIT(4)
#define NPM13XX_CHG_STATUS_RECHARGE_MASK  BIT(5)
#define NPM13XX_CHG_STATUS_DIETEMP_MASK   BIT(6)
#define NPM13XX_CHG_STATUS_SUPPLEMENT_MASK BIT(7)

static const struct device *const charger = DEVICE_DT_GET(CHARGER_NODE);
static bool charger_ready;

static const char *charge_status_str(int32_t chg_status)
{
    if (chg_status & NPM13XX_CHG_STATUS_COMPLETE_MASK) {
        return "complete";
    }
    if (chg_status & NPM13XX_CHG_STATUS_TRICKLE_MASK) {
        return "trickle";
    }
    if (chg_status & NPM13XX_CHG_STATUS_CC_MASK) {
        return "cc";
    }
    if (chg_status & NPM13XX_CHG_STATUS_CV_MASK) {
        return "cv";
    }
    if (chg_status & NPM13XX_CHG_STATUS_DIETEMP_MASK) {
        return "paused";
    }
    if (chg_status & NPM13XX_CHG_STATUS_RECHARGE_MASK) {
        return "recharge";
    }
    if (chg_status & NPM13XX_CHG_STATUS_SUPPLEMENT_MASK) {
        return "supplement";
    }
    return "idle";
}

static int read_sensors(struct battery_sample *out)
{
    struct sensor_value value;
    int ret;

    ret = sensor_sample_fetch(charger);
    if (ret < 0) {
        return ret;
    }

    ret = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
    if (ret < 0) {
        return ret;
    }
    out->voltage_v = (float)value.val1 + ((float)value.val2 / 1000000.0f);

    ret = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &value);
    if (ret < 0) {
        return ret;
    }
    out->temp_c = (float)value.val1 + ((float)value.val2 / 1000000.0f);

    ret = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
    if (ret < 0) {
        return ret;
    }
    out->current_ma =
        ((float)value.val1 + ((float)value.val2 / 1000000.0f)) * 1000.0f;

    ret = sensor_channel_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_STATUS, &value);
    if (ret < 0) {
        return ret;
    }
    out->chg_stat = (uint8_t)value.val1;
    out->status = charge_status_str(value.val1);

    ret = sensor_channel_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_ERROR, &value);
    if (ret < 0) {
        out->err = 0;
    } else {
        out->err = (uint8_t)value.val1;
    }

    ret = sensor_channel_get(charger, SENSOR_CHAN_DIE_TEMP, &value);
    if (ret < 0) {
        out->die_c = 0.0f;
    } else {
        out->die_c = (float)value.val1 + ((float)value.val2 / 1000000.0f);
    }

    ret = sensor_attr_get(charger,
                          (enum sensor_channel)SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
                          (enum sensor_attribute)SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT,
                          &value);
    if (ret < 0) {
        out->vbus = false;
    } else {
        out->vbus = value.val1 != 0;
    }

    out->valid = true;
    return 0;
}

int battery_init(void)
{
    charger_ready = false;

    if (!device_is_ready(charger)) {
        LOG_ERR("nPM1300 charger not ready");
        return -ENODEV;
    }

    struct battery_sample sample = {0};
    int err = read_sensors(&sample);
    if (err) {
        LOG_ERR("nPM1300 first read failed (err %d)", err);
        return err;
    }

    charger_ready = true;
    LOG_INF("nPM1300 ready  V=%.3f I=%.1f mA T=%.1f C die=%.1f C VBUS=%s %s chg_stat=0x%02x err=0x%02x",
            (double)sample.voltage_v, (double)sample.current_ma,
            (double)sample.temp_c, (double)sample.die_c,
            sample.vbus ? "yes" : "no", sample.status,
            sample.chg_stat, sample.err);
    return 0;
}

bool battery_is_ready(void)
{
    return charger_ready;
}

int battery_read(struct battery_sample *out)
{
    if (!out) {
        return -EINVAL;
    }

    *out = (struct battery_sample){0};

    if (!charger_ready) {
        return -ENODEV;
    }

    return read_sensors(out);
}

static int cmd_bat_read(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    struct battery_sample sample;
    int err = battery_read(&sample);
    if (err) {
        shell_error(sh, "battery read failed (err %d)", err);
        return err;
    }

    shell_print(sh, "V=%.3f I=%.1f mA T=%.1f C die=%.1f C VBUS=%s status=%s chg_stat=0x%02x err=0x%02x",
                (double)sample.voltage_v, (double)sample.current_ma,
                (double)sample.temp_c, (double)sample.die_c,
                sample.vbus ? "yes" : "no", sample.status,
                sample.chg_stat, sample.err);
    return 0;
}

SHELL_CMD_REGISTER(bat, NULL, "Read nPM1300 battery", cmd_bat_read);
