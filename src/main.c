/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * XIAO nRF54LM20A pH node.
 *
 *   - Atlas Scientific pH OEM on isolated I2C (D4/D5)
 *   - Optional DS18B20 temperature on D0 (J2)
 *   - nPM1300 battery voltage / current / charge status
 *   - Stream `.s/ph` to Golioth on each Pouch/BLE gateway sync
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(main);

#include "credentials.h"
#include "ble_peripheral.h"
#include "ph_oem.h"
#include "temp_sensor.h"
#include "battery.h"
#include "status_led.h"
#include "golioth_ph.h"
#include "sample_print.h"

#include <pouch/pouch.h>
#include <pouch/transport/bluetooth/gatt.h>

#include <app_version.h>

#define SAMPLE_STACK_SIZE 2048

static K_THREAD_STACK_DEFINE(sample_stack, SAMPLE_STACK_SIZE);
static struct k_thread sample_thread;
static bool sample_print_enabled = IS_ENABLED(CONFIG_SAMPLE_PRINT);
static bool sample_print_paused;

void sample_print_enable(bool on)
{
    sample_print_enabled = on;
}

bool sample_print_is_enabled(void)
{
    return sample_print_enabled;
}

void sample_print_pause(bool pause)
{
    sample_print_paused = pause;
}

static float sample_temp_c(void)
{
    float t;

    if (temp_sensor_read_c(&t) == 0) {
        return t;
    }
    return CONFIG_PH_DEFAULT_TEMP_C / 100.0f;
}

static void print_sample(void)
{
    float temp_c = sample_temp_c();
    struct ph_oem_sample ph = {0};
    struct battery_sample bat = {0};

    if (ph_oem_is_ready()) {
        int err = ph_oem_read(temp_c, &ph);
        if (err) {
            printk("ph: read failed (err %d)  temp=%.2f C\n", err, (double)temp_c);
        } else {
            printk("ph: %.3f  %.2f mV  %.2f C  cal=0x%02x\n",
                   (double)ph.ph, (double)ph.mv, (double)ph.temp_c, ph.cal_bits);
        }
    } else {
        printk("ph: OEM not found  temp=%.2f C\n", (double)temp_c);
    }

    if (battery_is_ready() && battery_read(&bat) == 0) {
        printk("bat: %.3f V  %.1f mA  %.1f C  die=%.1f C  VBUS=%s  %s  chg_stat=0x%02x err=0x%02x\n",
               (double)bat.voltage_v, (double)bat.current_ma, (double)bat.temp_c,
               (double)bat.die_c, bat.vbus ? "yes" : "no", bat.status,
               bat.chg_stat, bat.err);
    } else {
        printk("bat: nPM1300 not ready\n");
    }
}

static void sample_loop(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        if (sample_print_enabled && !sample_print_paused) {
            print_sample();
        }
        k_sleep(K_SECONDS(CONFIG_SAMPLE_PRINT_PERIOD_S));
    }
}

static int setup_pouch(void)
{
    struct pouch_config config = {0};

    int err = load_certificate(&config.certificate);
    if (err) {
        LOG_ERR("Failed to load certificate (err %d)", err);
        return err;
    }

    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL) {
        LOG_ERR("Failed to load private key");
        return -ENOENT;
    }

    LOG_INF("Credentials loaded");

    err = pouch_init(&config);
    if (err) {
        LOG_ERR("Pouch init failed (err %d)", err);
        return err;
    }

    LOG_INF("Pouch initialized");
    return 0;
}

int main(void)
{
    printk("\n*** xiao_ph boot  pouch-log enabled ***\n");
    LOG_INF("XIAO nRF54LM20A pH — Pouch/BLE");
    LOG_INF("App version: " STRINGIFY(APP_BUILD_VERSION));
    LOG_INF("Pouch protocol %d, BLE transport %d", POUCH_VERSION, POUCH_GATT_VERSION);

    int err = ph_oem_init();
    if (err) {
        LOG_WRN("pH OEM init failed (err %d) — advertising anyway", err);
    }

    err = temp_sensor_init();
    if (err) {
        LOG_INF("No DS18B20; compensation uses default or TEMP_OVERRIDE_C");
    }

    err = battery_init();
    if (err) {
        LOG_WRN("nPM1300 init failed (err %d) — pH print continues", err);
    }

    err = status_led_init();
    if (err) {
        LOG_WRN("RGB LED init failed (err %d)", err);
    }

    k_thread_create(&sample_thread, sample_stack, SAMPLE_STACK_SIZE,
                    sample_loop, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
    k_thread_name_set(&sample_thread, "sample");

    (void)golioth_ph_init();

    err = credentials_prepare();
    if (err) {
        LOG_WRN("LittleFS credentials dir not ready (err %d) — "
                "smpmgr upload will fail until /lfs1 is mounted", err);
    }

    err = ble_peripheral_init();
    if (err) {
        return err;
    }

    err = setup_pouch();
    if (err) {
        printk("pouch: not started (err %d) — upload crt.der/key.der then reboot\n",
               err);
        LOG_WRN("Pouch not started (err %d) — serial still works; "
                "upload crt.der/key.der then reboot", err);
    } else {
        printk("pouch: ready, advertising as %s for %u s\n",
               CONFIG_BT_DEVICE_NAME, CONFIG_EXAMPLE_ADV_WINDOW_S);
        printk("local 5s log %s (ph log on|off)\n",
               sample_print_enabled ? "on" : "off");
    }

    err = ble_peripheral_start();
    if (err) {
        return err;
    }

    LOG_INF("Advertising started as %s (sync requested, %u s window)",
            CONFIG_BT_DEVICE_NAME, CONFIG_EXAMPLE_ADV_WINDOW_S);

    return 0;
}
