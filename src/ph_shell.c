/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 *
 * Serial shell for Atlas pH OEM bring-up:
 *   ph info
 *   ph read
 *   ph temp 25.0
 *   ph led on|off
 *   ph cal clear|low|mid|high
 *   ph log on|off
 */

#include "ph_oem.h"
#include "sample_print.h"
#include "temp_sensor.h"

#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint8_t type = 0;
    uint8_t fw = 0;
    int err = ph_oem_read_device_info(&type, &fw);
    if (err) {
        shell_error(sh, "read failed (err %d)", err);
        return err;
    }

    uint8_t cal = 0;
    (void)ph_oem_read_cal_confirm(&cal);
    shell_print(sh, "type=0x%02x fw=0x%02x cal_bits=0x%02x ready=%d",
                type, fw, cal, (int)ph_oem_is_ready());
    return 0;
}

static int cmd_read(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    float temp_c;
    if (temp_sensor_read_c(&temp_c) != 0) {
        temp_c = CONFIG_PH_DEFAULT_TEMP_C / 100.0f;
    }

    struct ph_oem_sample sample;
    int err = ph_oem_read(temp_c, &sample);
    if (err) {
        shell_error(sh, "read failed (err %d)", err);
        return err;
    }

    shell_print(sh, "pH=%.3f mV=%.2f temp_c=%.2f cal=0x%02x",
                (double)sample.ph, (double)sample.mv, (double)sample.temp_c,
                sample.cal_bits);
    return 0;
}

static int cmd_temp(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "usage: ph temp <celsius>");
        return -EINVAL;
    }

    float t = (float)atof(argv[1]);
    int err = ph_oem_set_temp_c(t);
    if (err) {
        shell_error(sh, "temp write failed (err %d)", err);
        return err;
    }

    shell_print(sh, "compensation set to %.2f C", (double)t);
    return 0;
}

static int cmd_led(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "usage: ph led on|off");
        return -EINVAL;
    }

    bool on = (argv[1][0] == '1') || (argv[1][0] == 'o' && argv[1][1] == 'n');
    int err = ph_oem_set_led(on);
    if (err) {
        shell_error(sh, "led failed (err %d)", err);
        return err;
    }

    shell_print(sh, "OEM LED %s", on ? "on" : "off");
    return 0;
}

static int cmd_cal(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "usage: ph cal clear|low|mid|high");
        return -EINVAL;
    }

    int cmd = 0;
    if (strcmp(argv[1], "clear") == 0) {
        cmd = 1;
    } else if (strcmp(argv[1], "low") == 0) {
        cmd = 2;
    } else if (strcmp(argv[1], "mid") == 0) {
        cmd = 3;
    } else if (strcmp(argv[1], "high") == 0) {
        cmd = 4;
    } else {
        shell_error(sh, "unknown cal command");
        return -EINVAL;
    }

    int err = ph_oem_calibrate(cmd);
    if (err) {
        shell_error(sh, "cal failed (err %d)", err);
        return err;
    }

    uint8_t bits = 0;
    (void)ph_oem_read_cal_confirm(&bits);
    shell_print(sh, "cal ok, bits=0x%02x", bits);
    return 0;
}

static int cmd_log(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_print(sh, "local log %s", sample_print_is_enabled() ? "on" : "off");
        return 0;
    }

    bool on = (argv[1][0] == '1') || (argv[1][0] == 'o' && argv[1][1] == 'n');
    bool off = (argv[1][0] == '0') || (strcmp(argv[1], "off") == 0);
    if (!on && !off) {
        shell_error(sh, "usage: ph log [on|off]");
        return -EINVAL;
    }

    sample_print_enable(on);
    shell_print(sh, "local log %s", on ? "on" : "off");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(ph_cmds,
    SHELL_CMD(info, NULL, "Read OEM device type / firmware", cmd_info),
    SHELL_CMD(read, NULL, "Take a pH reading", cmd_read),
    SHELL_CMD(temp, NULL, "Set compensation temperature", cmd_temp),
    SHELL_CMD(led, NULL, "OEM LED on|off", cmd_led),
    SHELL_CMD(cal, NULL, "Calibrate clear|low|mid|high", cmd_cal),
    SHELL_CMD(log, NULL, "Local 5s serial print on|off", cmd_log),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ph, &ph_cmds, "Atlas pH OEM", NULL);
