/*
 * Copyright (c) 2026 Golioth, Inc.
 * Copyright (c) 2026 Brian Wischmeyer
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BLE peripheral for the Pouch GATT transport. Advertises only when a
 * sync is due, then sleeps with the radio off until the next interval.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_peripheral);

#include "ble_peripheral.h"
#include "power_sleep.h"
#include "status_led.h"

#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>

#include <pouch/transport/bluetooth/gatt.h>
#include <pouch/types.h>

#define STATUS_PERIOD_S 10

static struct bt_conn *default_conn;
static uint32_t sync_period_s = CONFIG_EXAMPLE_SYNC_PERIOD_S;
static bool conn_sync_ok;
static uint8_t bounce_retries;

static struct pouch_gatt_adv service_data = POUCH_GATT_ADV_DATA_INIT;

struct pouch_gatt_adv_128 {
    uint8_t uuid[16];
    struct pouch_gatt_adv_data payload;
} __packed;

static struct pouch_gatt_adv_128 service_data_128 = {
    .uuid = {BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d272)},
    .payload = {
        .version = (POUCH_VERSION << POUCH_GATT_ADV_VERSION_POUCH_SHIFT)
            | (POUCH_GATT_VERSION << POUCH_GATT_ADV_VERSION_SELF_SHIFT),
        .flags = 0x0,
    },
};

/* Name goes in the scan response so both 16- and 128-bit service data fit. */
static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_SVC_DATA16, &service_data, sizeof(service_data)),
    BT_DATA(BT_DATA_SVC_DATA128, &service_data_128, sizeof(service_data_128)),
};

static struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static bool sync_requested(void)
{
    return (service_data.payload.flags & POUCH_GATT_ADV_FLAG_SYNC_REQUEST) != 0;
}

static void peer_str(struct bt_conn *conn, char *buf, size_t len)
{
    bt_addr_le_to_str(bt_conn_get_dst(conn), buf, len);
}

static void status_work_handler(struct k_work *work);
static void sleep_wake_work_handler(struct k_work *work);
static void adv_timeout_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(status_work, status_work_handler);
K_WORK_DELAYABLE_DEFINE(sleep_wake_work, sleep_wake_work_handler);
K_WORK_DELAYABLE_DEFINE(adv_timeout_work, adv_timeout_work_handler);

static void status_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (default_conn) {
        return;
    }

    printk("pouch: waiting  name=%s  sync_req=%d  uuid=0x%04x\n",
           CONFIG_BT_DEVICE_NAME, (int)sync_requested(), POUCH_GATT_UUID_SVC_VAL_16);
    k_work_schedule(&status_work, K_SECONDS(STATUS_PERIOD_S));
}

static void ble_adv_stop(void)
{
    k_work_cancel_delayable(&status_work);
    k_work_cancel_delayable(&adv_timeout_work);
    k_work_cancel_delayable(&sleep_wake_work);

    int err = bt_le_adv_stop();
    if (err && err != -EALREADY) {
        printk("pouch: advertising stop failed (err %d)\n", err);
        LOG_WRN("Advertising stop failed (err %d)", err);
    }
}

static void schedule_sleep(void)
{
    bounce_retries = 0;
    ble_adv_stop();
    status_led_off();
    power_sleep_enter();
    printk("pouch: sleeping %u s (radio off)\n", sync_period_s);
    LOG_INF("Sleeping %u s with radio off", sync_period_s);
    k_work_schedule(&sleep_wake_work, K_SECONDS(sync_period_s));
}

static void sleep_wake_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    power_sleep_exit();
    printk("pouch: waking\n");
    LOG_INF("Waking for gateway sync");
    int err = ble_peripheral_start();
    if (err) {
        printk("pouch: advertising start failed (err %d), sleep again\n", err);
        schedule_sleep();
    }
}

static void adv_timeout_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (default_conn) {
        return;
    }

    printk("pouch: no gateway in %u s, sleeping\n", CONFIG_EXAMPLE_ADV_WINDOW_S);
    LOG_INF("Advertise window expired, sleeping");
    schedule_sleep();
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    peer_str(conn, addr, sizeof(addr));
    if (err) {
        printk("pouch: BLE connect failed from %s (err 0x%02x)\n", addr, err);
        LOG_WRN("Connection failed from %s (err 0x%02x)", addr, err);
        return;
    }

    default_conn = conn;
    conn_sync_ok = false;
    k_work_cancel_delayable(&adv_timeout_work);
    k_work_cancel_delayable(&status_work);
    status_led_on();
    printk("pouch: BLE connected  peer=%s\n", addr);
    LOG_INF("Gateway connected %s", addr);
}

void ble_peripheral_mark_sync_ok(void)
{
    conn_sync_ok = true;
    bounce_retries = 0;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    peer_str(conn, addr, sizeof(addr));
    printk("pouch: BLE disconnected  peer=%s  reason=0x%02x\n", addr, reason);
    LOG_INF("Gateway disconnected %s (reason 0x%02x)", addr, reason);

    default_conn = NULL;
    status_led_off();

    if (conn_sync_ok) {
        bounce_retries = 0;
        schedule_sleep();
        return;
    }

    if (bounce_retries < CONFIG_EXAMPLE_ADV_RETRIES) {
        bounce_retries++;
        printk("pouch: bounce, retry advertise %u/%u\n",
               bounce_retries, CONFIG_EXAMPLE_ADV_RETRIES);
        LOG_INF("Retry advertise after bounce %u/%u",
                bounce_retries, CONFIG_EXAMPLE_ADV_RETRIES);
        int err = ble_peripheral_start();
        if (err) {
            bounce_retries = 0;
            schedule_sleep();
        }
        return;
    }

    printk("pouch: bounce retries exhausted, sleeping\n");
    LOG_INF("Bounce retries exhausted, sleeping");
    bounce_retries = 0;
    schedule_sleep();
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    peer_str(conn, addr, sizeof(addr));
    if (err) {
        printk("pouch: pairing failed  peer=%s  err=%d\n", addr, (int)err);
        LOG_WRN("Security failed for %s (err %d)", addr, err);
        return;
    }

    printk("pouch: pairing ok  peer=%s  level=%d\n", addr, (int)level);
    LOG_INF("Security changed %s level %d", addr, level);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    char passkey_str[7];

    peer_str(conn, addr, sizeof(addr));
    (void)snprintf(passkey_str, sizeof(passkey_str), "%06u", passkey);
    printk("pouch: passkey for %s: %s\n", addr, passkey_str);
    LOG_INF("Passkey for %s: %s", addr, passkey_str);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    char passkey_str[7];

    peer_str(conn, addr, sizeof(addr));
    (void)snprintf(passkey_str, sizeof(passkey_str), "%06u", passkey);
    printk("pouch: confirm passkey for %s: %s\n", addr, passkey_str);
    LOG_INF("Confirm passkey for %s: %s", addr, passkey_str);

    if (IS_ENABLED(CONFIG_APP_BT_AUTO_CONFIRM)) {
        printk("pouch: auto-confirming passkey\n");
        LOG_INF("Auto-confirming passkey (CONFIG_APP_BT_AUTO_CONFIRM)");
        bt_conn_auth_passkey_confirm(conn);
    }
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    peer_str(conn, addr, sizeof(addr));
    printk("pouch: pairing cancelled  peer=%s\n", addr);
    LOG_INF("Pairing cancelled: %s", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .passkey_confirm = auth_passkey_confirm,
    .cancel = auth_cancel,
};

void ble_peripheral_set_sync_period_s(uint32_t seconds)
{
    if (seconds < 5) {
        seconds = 5;
    }
    if (seconds > 3600) {
        seconds = 3600;
    }
    sync_period_s = seconds;
    printk("pouch: sync period %u s\n", sync_period_s);
    LOG_INF("Sync period set to %u s", sync_period_s);
}

uint32_t ble_peripheral_get_sync_period_s(void)
{
    return sync_period_s;
}

void ble_peripheral_request_gateway(bool request)
{
    pouch_gatt_adv_req_sync(&service_data, request);
    service_data_128.payload = service_data.payload;
    int err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    printk("pouch: adv sync_req=%d  update_err=%d\n", (int)request, err);
}

void ble_peripheral_button_handler(void)
{
    if (default_conn) {
        printk("pouch: confirming passkey\n");
        LOG_INF("Confirming passkey");
        bt_conn_auth_passkey_confirm(default_conn);
    } else {
        printk("pouch: no BLE connection for passkey confirm\n");
        LOG_WRN("No BT connection for passkey confirmation");
    }
}

int ble_peripheral_init(void)
{
    int err = bt_enable(NULL);
    if (err) {
        printk("pouch: Bluetooth init failed (err %d)\n", err);
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    bt_addr_le_t id_addr;
    size_t count = 1;

    bt_id_get(&id_addr, &count);
    if (count > 0) {
        char addr[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(&id_addr, addr, sizeof(addr));
        printk("pouch: BLE identity %s\n", addr);
    }

    err = bt_conn_auth_cb_register(&auth_cb_display);
    if (err) {
        printk("pouch: auth callback register failed (err %d)\n", err);
        LOG_ERR("Bluetooth auth cb register failed (err %d)", err);
        return err;
    }

    return 0;
}

int ble_peripheral_start(void)
{
    pouch_gatt_adv_req_sync(&service_data, true);
    service_data_128.payload = service_data.payload;

    int err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
                                              BT_GAP_ADV_FAST_INT_MIN_1,
                                              BT_GAP_ADV_FAST_INT_MAX_1,
                                              NULL),
                              ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err == -EALREADY) {
        err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    }
    if (err) {
        printk("pouch: advertising start  name=%s  sync_req=1  err=%d\n",
               CONFIG_BT_DEVICE_NAME, err);
        return err;
    }

    printk("pouch: advertising  name=%s  sync_req=1  window=%u s\n",
           CONFIG_BT_DEVICE_NAME, CONFIG_EXAMPLE_ADV_WINDOW_S);
    k_work_schedule(&adv_timeout_work, K_SECONDS(CONFIG_EXAMPLE_ADV_WINDOW_S));
    k_work_schedule(&status_work, K_NO_WAIT);
    return 0;
}

void ble_peripheral_status_start(void)
{
    k_work_schedule(&status_work, K_NO_WAIT);
}
