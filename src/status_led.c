/*
 * Copyright (c) 2026 Brian Wischmeyer
 * SPDX-License-Identifier: Apache-2.0
 */

#include "status_led.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(status_led, CONFIG_LOG_DEFAULT_LEVEL);

#define PWM_PERIOD_NS 1000000UL
#define RAINBOW_STEP_MS 30
#define RAINBOW_STACK_SIZE 1024

static const struct pwm_dt_spec pwm_red = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
static const struct pwm_dt_spec pwm_blue = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1));
static const struct pwm_dt_spec pwm_green = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));

static K_THREAD_STACK_DEFINE(rainbow_stack, RAINBOW_STACK_SIZE);
static struct k_thread rainbow_thread;
static atomic_t rainbow_on;

static uint32_t level_to_pulse(uint8_t level)
{
    return (PWM_PERIOD_NS * (uint32_t)level) / 255U;
}

static void hsv_to_rgb(uint16_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint8_t region = (uint8_t)(hue / 60U);
    const uint16_t remainder = (hue % 60U) * 255U / 60U;
    const uint8_t p = 0;
    const uint8_t q = (uint8_t)(255U - remainder);
    const uint8_t t = (uint8_t)remainder;

    switch (region) {
    case 0:
        *r = 255;
        *g = t;
        *b = p;
        break;
    case 1:
        *r = q;
        *g = 255;
        *b = p;
        break;
    case 2:
        *r = p;
        *g = 255;
        *b = t;
        break;
    case 3:
        *r = p;
        *g = q;
        *b = 255;
        break;
    case 4:
        *r = t;
        *g = p;
        *b = 255;
        break;
    default:
        *r = 255;
        *g = p;
        *b = q;
        break;
    }

    /* Keep it readable, not blinding. */
    *r = (uint8_t)(*r / 3U);
    *g = (uint8_t)(*g / 3U);
    *b = (uint8_t)(*b / 3U);
}

static int set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    int err;

    err = pwm_set_dt(&pwm_red, PWM_PERIOD_NS, level_to_pulse(r));
    if (err) {
        return err;
    }
    err = pwm_set_dt(&pwm_green, PWM_PERIOD_NS, level_to_pulse(g));
    if (err) {
        return err;
    }
    return pwm_set_dt(&pwm_blue, PWM_PERIOD_NS, level_to_pulse(b));
}

static void rainbow_loop(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint16_t hue = 0;

    while (1) {
        uint8_t r;
        uint8_t g;
        uint8_t b;

        if (!atomic_get(&rainbow_on)) {
            (void)set_rgb(0, 0, 0);
            k_sleep(K_MSEC(500));
            continue;
        }

        hsv_to_rgb(hue, &r, &g, &b);
        (void)set_rgb(r, g, b);
        hue = (uint16_t)((hue + 2U) % 360U);
        k_msleep(RAINBOW_STEP_MS);
    }
}

int status_led_init(void)
{
    if (!pwm_is_ready_dt(&pwm_red) || !pwm_is_ready_dt(&pwm_green) ||
        !pwm_is_ready_dt(&pwm_blue)) {
        LOG_ERR("RGB PWM not ready");
        return -ENODEV;
    }

    (void)set_rgb(0, 0, 0);
    atomic_set(&rainbow_on, 0);

    k_thread_create(&rainbow_thread, rainbow_stack, RAINBOW_STACK_SIZE,
                    rainbow_loop, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(9), 0, K_NO_WAIT);
    k_thread_name_set(&rainbow_thread, "rgb");
    LOG_INF("XIAO RGB off (on while gateway is connected)");
    return 0;
}

void status_led_on(void)
{
    atomic_set(&rainbow_on, 1);
}

void status_led_off(void)
{
    atomic_set(&rainbow_on, 0);
    (void)set_rgb(0, 0, 0);
}
