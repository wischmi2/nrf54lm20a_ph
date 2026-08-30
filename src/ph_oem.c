/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * Atlas Scientific pH OEM register driver (SMBus/I2C, default 0x65).
 *
 * Atlas requires a STOP after the register-pointer write, then a separate
 * read. Do not use i2c_write_read() (repeated START).
 */

#include "ph_oem.h"
#include "power_sleep.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(ph_oem, CONFIG_LOG_DEFAULT_LEVEL);

#define PH_OEM_NODE DT_NODELABEL(atlas_ph)

#if DT_NODE_EXISTS(PH_OEM_NODE)
static const struct i2c_dt_spec ph_i2c = I2C_DT_SPEC_GET(PH_OEM_NODE);
#else
static const struct i2c_dt_spec ph_i2c = {
    .bus = DEVICE_DT_GET(DT_NODELABEL(i2c22)),
    .addr = CONFIG_PH_OEM_I2C_ADDR,
};
#endif

#define REG_DEVICE_TYPE 0x00
#define REG_FW_VERSION 0x01
#define REG_LED 0x05
#define REG_HIBERNATE 0x06
#define REG_NEW_READING 0x07
#define REG_CAL_VALUE 0x08
#define REG_CAL_REQUEST 0x0C
#define REG_CAL_CONFIRM 0x0D
#define REG_TEMP_COMP 0x0E
#define REG_PH 0x16
#define REG_MV 0x1A

#define DEVICE_TYPE_PH 0x01

#define CAL_CLEAR 1
#define CAL_LOW 2
#define CAL_MID 3
#define CAL_HIGH 4

static bool oem_ready;

static int write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[5];

    if (len > 4) {
        return -EINVAL;
    }

    buf[0] = reg;
    if (len > 0) {
        memcpy(&buf[1], data, len);
    }

    power_sleep_buses_resume();
    return i2c_write_dt(&ph_i2c, buf, len + 1);
}

static int write_u8(uint8_t reg, uint8_t value)
{
    return write_reg(reg, &value, 1);
}

static int read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    power_sleep_buses_resume();
    int err = i2c_write_dt(&ph_i2c, &reg, 1);
    if (err) {
        return err;
    }

    return i2c_read_dt(&ph_i2c, data, len);
}

static bool is_finite_f(float v)
{
    return v == v;
}

static int32_t scale_int(float v, float mul)
{
    return (int32_t)(v * mul + (v >= 0.0f ? 0.5f : -0.5f));
}

static int32_t be32_to_s32(const uint8_t *b)
{
    return (int32_t)sys_get_be32(b);
}

static void s32_to_be32(uint8_t *b, int32_t v)
{
    sys_put_be32((uint32_t)v, b);
}

int ph_oem_init(void)
{
    oem_ready = false;

    if (!i2c_is_ready_dt(&ph_i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    uint8_t type = 0;
    uint8_t fw = 0;
    int err = ph_oem_read_device_info(&type, &fw);
    if (err) {
        LOG_ERR("Atlas pH OEM not found at 0x%02x (err %d)", ph_i2c.addr, err);
        return err;
    }

    if (type != DEVICE_TYPE_PH) {
        LOG_ERR("Unexpected device type 0x%02x (expected 0x%02x)", type, DEVICE_TYPE_PH);
        return -EIO;
    }

    err = ph_oem_set_led(false);
    if (err) {
        LOG_WRN("Failed to turn OEM LED off (err %d)", err);
    }

    err = ph_oem_set_active(false);
    if (err) {
        LOG_WRN("Failed to hibernate OEM (err %d)", err);
    }

    oem_ready = true;
    LOG_INF("Atlas pH OEM ready (fw 0x%02x)", fw);
    return 0;
}

bool ph_oem_is_ready(void)
{
    return oem_ready;
}

int ph_oem_read_device_info(uint8_t *type, uint8_t *fw)
{
    uint8_t buf[2];
    int err = read_regs(REG_DEVICE_TYPE, buf, sizeof(buf));
    if (err) {
        return err;
    }

    if (type) {
        *type = buf[0];
    }
    if (fw) {
        *fw = buf[1];
    }
    return 0;
}

int ph_oem_set_active(bool active)
{
    return write_u8(REG_HIBERNATE, active ? 0x01 : 0x00);
}

int ph_oem_set_led(bool on)
{
    return write_u8(REG_LED, on ? 0x01 : 0x00);
}

int ph_oem_set_temp_c(float temp_c)
{
    uint8_t buf[4];

    s32_to_be32(buf, scale_int(temp_c, 100.0f));
    return write_reg(REG_TEMP_COMP, buf, sizeof(buf));
}

int ph_oem_read_cal_confirm(uint8_t *bits)
{
    return read_regs(REG_CAL_CONFIRM, bits, 1);
}

static int wait_new_reading(void)
{
    const int64_t deadline = k_uptime_get() + CONFIG_PH_OEM_READ_TIMEOUT_MS;

    while (k_uptime_get() < deadline) {
        uint8_t flag = 0;
        int err = read_regs(REG_NEW_READING, &flag, 1);
        if (err) {
            return err;
        }
        if (flag == 1) {
            return 0;
        }
        k_sleep(K_MSEC(20));
    }

    return -ETIMEDOUT;
}

int ph_oem_read(float temp_c, struct ph_oem_sample *out)
{
    if (!out) {
        return -EINVAL;
    }

    memset(out, 0, sizeof(*out));

    int err = ph_oem_set_active(true);
    if (err) {
        return err;
    }

    if (is_finite_f(temp_c)) {
        err = ph_oem_set_temp_c(temp_c);
        if (err) {
            LOG_WRN("Temp compensate failed (err %d)", err);
        } else {
            out->temp_c = temp_c;
        }
    }

    err = wait_new_reading();
    if (err) {
        (void)ph_oem_set_active(false);
        return err;
    }

    uint8_t raw[8];
    err = read_regs(REG_PH, raw, sizeof(raw));
    if (err) {
        (void)ph_oem_set_active(false);
        return err;
    }

    out->ph = be32_to_s32(&raw[0]) / 1000.0f;
    out->mv = be32_to_s32(&raw[4]) / 1000.0f;

    (void)ph_oem_read_cal_confirm(&out->cal_bits);

    err = write_u8(REG_NEW_READING, 0x00);
    if (err) {
        LOG_WRN("Failed to clear new-reading flag (err %d)", err);
    }

    (void)ph_oem_set_active(false);

    out->valid = true;
    return 0;
}

int ph_oem_calibrate(int cmd)
{
    if (cmd < CAL_CLEAR || cmd > CAL_HIGH) {
        return -EINVAL;
    }

    if (cmd != CAL_CLEAR) {
        float cal_ph;

        switch (cmd) {
        case CAL_LOW:
            cal_ph = 4.000f;
            break;
        case CAL_MID:
            cal_ph = 7.000f;
            break;
        default:
            cal_ph = 10.000f;
            break;
        }

        uint8_t buf[4];
        s32_to_be32(buf, scale_int(cal_ph, 1000.0f));
        int err = write_reg(REG_CAL_VALUE, buf, sizeof(buf));
        if (err) {
            return err;
        }
    }

    int err = write_u8(REG_CAL_REQUEST, (uint8_t)cmd);
    if (err) {
        return err;
    }

    /* Calibration completes on the I2C STOP of the request write. Confirm. */
    k_sleep(K_MSEC(50));

    uint8_t bits = 0;
    err = ph_oem_read_cal_confirm(&bits);
    if (err) {
        return err;
    }

    LOG_INF("Calibration cmd %d done, confirm bits 0x%02x", cmd, bits);
    return 0;
}
