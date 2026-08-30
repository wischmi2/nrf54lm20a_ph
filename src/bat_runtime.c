#include "bat_runtime.h"

#include "battery.h"

#include <stdio.h>
#include <stdlib.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(bat_runtime);

#define RUN_PATH "/lfs1/bat_run.txt"
#define POLL_USB_S 5
#define POLL_BAT_S 60

static bool vbus_known;
static bool vbus_on;
static bool on_battery;
static int64_t unplug_ms;
static uint32_t last_saved_s;

static void poll_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(poll_work, poll_work_handler);

static uint32_t elapsed_s(void)
{
    if (!on_battery) {
        return 0;
    }

    int64_t ms = k_uptime_get() - unplug_ms;

    if (ms < 0) {
        return 0;
    }
    return (uint32_t)(ms / 1000);
}

static void persist(bool running, uint32_t secs, int v_mv)
{
    struct fs_file_t file;
    char line[48];
    int len = snprintf(line, sizeof(line), "%u %u %d\n",
                       running ? 1U : 0U, secs, v_mv);

    if (len <= 0) {
        return;
    }

    fs_file_t_init(&file);
    int err = fs_open(&file, RUN_PATH, FS_O_CREATE | FS_O_WRITE);
    if (err) {
        LOG_WRN("bat_run write open failed (err %d)", err);
        return;
    }

    (void)fs_seek(&file, 0, FS_SEEK_SET);
    (void)fs_truncate(&file, 0);
    (void)fs_write(&file, line, (size_t)len);
    fs_close(&file);
    last_saved_s = secs;
}

static void load_previous(void)
{
    struct fs_file_t file;
    char buf[48];
    unsigned running = 0;
    unsigned secs = 0;
    int v_mv = 0;

    fs_file_t_init(&file);
    int err = fs_open(&file, RUN_PATH, FS_O_READ);
    if (err) {
        return;
    }

    ssize_t n = fs_read(&file, buf, sizeof(buf) - 1);
    fs_close(&file);
    if (n <= 0) {
        return;
    }
    buf[n] = '\0';

    char *end = buf;

    running = (unsigned)strtoul(end, &end, 10);
    secs = (unsigned)strtoul(end, &end, 10);
    v_mv = (int)strtol(end, &end, 10);

    if (running && secs > 0) {
        printk("bat: last USB-unplugged run was at least %u s (%u min) @ %d mV"
               " — reset or pack died\n",
               secs, secs / 60, v_mv);
        LOG_INF("Previous battery run >= %u s at %d mV", secs, v_mv);
    } else if (secs > 0) {
        printk("bat: last USB-unplugged run lasted %u s (%u min)\n",
               secs, secs / 60);
    }
}

static void schedule_poll(void)
{
    uint32_t s = vbus_on ? POLL_USB_S : POLL_BAT_S;

    k_work_schedule(&poll_work, K_SECONDS(s));
}

static void handle_sample(const struct battery_sample *bat)
{
    const int v_mv = (int)(bat->voltage_v * 1000.0f + 0.5f);

    if (!vbus_known) {
        vbus_known = true;
        vbus_on = bat->vbus;
        if (!bat->vbus) {
            on_battery = true;
            unplug_ms = k_uptime_get();
            printk("bat: boot on battery  %.3f V\n", (double)bat->voltage_v);
            persist(true, 0, v_mv);
        } else {
            printk("bat: USB present  %.3f V\n", (double)bat->voltage_v);
        }
        schedule_poll();
        return;
    }

    if (vbus_on && !bat->vbus) {
        on_battery = true;
        unplug_ms = k_uptime_get();
        last_saved_s = 0;
        printk("bat: USB unplugged  running on battery  %.3f V\n",
               (double)bat->voltage_v);
        LOG_INF("USB unplugged, on battery %.3f V", (double)bat->voltage_v);
        persist(true, 0, v_mv);
    } else if (!vbus_on && bat->vbus) {
        uint32_t secs = elapsed_s();

        printk("bat: USB back  ran %u s (%u min %u s)  %.3f V\n",
               secs, secs / 60, secs % 60, (double)bat->voltage_v);
        LOG_INF("USB back after %u s", secs);
        persist(false, secs, v_mv);
        on_battery = false;
    } else if (on_battery) {
        uint32_t secs = elapsed_s();

        if (secs >= last_saved_s + POLL_BAT_S) {
            persist(true, secs, v_mv);
            printk("bat: on battery %u s (%u min)  %.3f V\n",
                   secs, secs / 60, (double)bat->voltage_v);
        }
    }

    vbus_on = bat->vbus;
    schedule_poll();
}

static void poll_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    bat_runtime_poll();
}

void bat_runtime_poll(void)
{
    struct battery_sample bat = {0};

    if (!battery_is_ready() || battery_read(&bat) != 0) {
        schedule_poll();
        return;
    }

    handle_sample(&bat);
}

uint32_t bat_runtime_on_bat_s(void)
{
    return elapsed_s();
}

void bat_runtime_init(void)
{
    load_previous();

    if (battery_is_ready()) {
        bat_runtime_poll();
    } else {
        k_work_schedule(&poll_work, K_SECONDS(POLL_USB_S));
    }
}
