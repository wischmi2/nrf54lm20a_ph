/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * Optional DS18B20 on XIAO D0 (P1.0 / TEMP_DQ). Missing hardware is fine.
 */

#include "temp_sensor.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(temp_sensor, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_HAS_STATUS(DT_ALIAS(ds18b20), okay)
#define HAS_DS18B20 1
static const struct device *const ds18b20 = DEVICE_DT_GET(DT_ALIAS(ds18b20));
#else
#define HAS_DS18B20 0
#endif

static bool sensor_ready;

int temp_sensor_init(void)
{
    sensor_ready = false;

#if HAS_DS18B20
    if (!device_is_ready(ds18b20)) {
        LOG_WRN("DS18B20 not ready — using default/override temperature");
        return -ENODEV;
    }

    /* Driver can be ready with no probe on the bus. Probe once. */
    if (sensor_sample_fetch(ds18b20)) {
        LOG_INF("No DS18B20 on D0; using default/override temperature");
        return -ENODEV;
    }

    sensor_ready = true;
    LOG_INF("DS18B20 ready on D0");
    return 0;
#else
    LOG_INF("No DS18B20 in devicetree");
    return -ENODEV;
#endif
}

bool temp_sensor_is_ready(void)
{
    return sensor_ready;
}

int temp_sensor_read_c(float *temp_c)
{
    if (!temp_c) {
        return -EINVAL;
    }

#if HAS_DS18B20
    if (!sensor_ready) {
        return -ENODEV;
    }

    int err = sensor_sample_fetch(ds18b20);
    if (err) {
        sensor_ready = false;
        LOG_INF("DS18B20 disappeared (err %d); using default/override", err);
        return err;
    }

    struct sensor_value val;
    err = sensor_channel_get(ds18b20, SENSOR_CHAN_AMBIENT_TEMP, &val);
    if (err) {
        return err;
    }

    *temp_c = sensor_value_to_float(&val);
    return 0;
#else
    return -ENODEV;
#endif
}
