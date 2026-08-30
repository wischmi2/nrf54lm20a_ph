/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 */

#include "power_sleep.h"

#include "ph_oem.h"
#include "sample_print.h"
#include "status_led.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#if IS_ENABLED(CONFIG_PM_DEVICE)
#include <zephyr/pm/device.h>
#endif

LOG_MODULE_REGISTER(power_sleep);

static bool buses_suspended;

#if IS_ENABLED(CONFIG_PM_DEVICE)
static const struct device *i2c_bus(void)
{
    return DEVICE_DT_GET(DT_NODELABEL(i2c22));
}
#endif

static void buses_suspend(void)
{
#if IS_ENABLED(CONFIG_PM_DEVICE)
    const struct device *i2c = i2c_bus();

    if (!device_is_ready(i2c) || buses_suspended) {
        return;
    }

    int err = pm_device_action_run(i2c, PM_DEVICE_ACTION_SUSPEND);
    if (err && err != -EALREADY) {
        LOG_WRN("I2C suspend failed (err %d)", err);
        return;
    }

    buses_suspended = true;
#else
    ARG_UNUSED(buses_suspended);
#endif
}

void power_sleep_buses_resume(void)
{
#if IS_ENABLED(CONFIG_PM_DEVICE)
    const struct device *i2c = i2c_bus();

    if (!buses_suspended) {
        return;
    }

    int err = pm_device_action_run(i2c, PM_DEVICE_ACTION_RESUME);
    if (err && err != -EALREADY) {
        LOG_WRN("I2C resume failed (err %d)", err);
        return;
    }

    buses_suspended = false;
#endif
}

void power_sleep_enter(void)
{
    sample_print_pause(true);
    status_led_off();

    if (ph_oem_is_ready()) {
        (void)ph_oem_set_led(false);
        (void)ph_oem_set_active(false);
    }

    buses_suspend();
    printk("power: sleep enter  atlas hibernate  rgb off  i2c %s\n",
           buses_suspended ? "suspend" : "up");
}

void power_sleep_exit(void)
{
    power_sleep_buses_resume();
    sample_print_pause(false);
    printk("power: sleep exit\n");
}
