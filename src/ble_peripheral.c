/*
 * Copyright (c) 2026 Golioth, Inc.
 * Copyright (c) 2026 Brian Wischmeyer
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BLE peripheral for the Pouch GATT transport. Advertises the Pouch service,
 * asks a nearby Golioth gateway to sync, and handles pairing.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_peripheral);

#include "ble_peripheral.h"

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
K_WORK_DELAYABLE_DEFINE(status_work, status_work_handler);

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
    k_work_cancel_delayable(&status_work);
    printk("pouch: BLE connected  peer=%s\n", addr);
    LOG_INF("Gateway connected %s", addr);
}

static void sync_request_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    printk("pouch: setting sync request flag\n");
    LOG_INF("Requesting gateway sync");
    ble_peripheral_request_gateway(true);
}
K_WORK_DELAYABLE_DEFINE(sync_request_work, sync_request_work_handler);

static void resume_advertising(struct k_work *work)
{
    ARG_UNUSED(work);

    int err = ble_peripheral_start();
    if (err) {
        printk("pouch: advertising restart failed (err %d)\n", err);
        LOG_ERR("Failed to start advertising (err: %d)", err);
        return;
    }

    printk("pouch: advertising again  sync_req=0  next request in %u s\n", sync_period_s);
    k_work_schedule(&sync_request_work, K_SECONDS(sync_period_s));
    k_work_schedule(&status_work, K_SECONDS(STATUS_PERIOD_S));
}
K_WORK_DEFINE(resume_work, resume_advertising);

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    peer_str(conn, addr, sizeof(addr));
    printk("pouch: BLE disconnected  peer=%s  reason=0x%02x\n", addr, reason);
    LOG_INF("Gateway disconnected %s (reason 0x%02x)", addr, reason);

    default_conn = NULL;
    k_work_submit(&resume_work);
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
    pouch_gatt_adv_req_sync(&service_data, false);
    service_data_128.payload = service_data.payload;
    int err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
                                              BT_GAP_ADV_FAST_INT_MIN_1,
                                              BT_GAP_ADV_FAST_INT_MAX_1,
                                              NULL),
                              ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    printk("pouch: advertising start  name=%s  sync_req=0  err=%d\n",
           CONFIG_BT_DEVICE_NAME, err);
    return err;
}

void ble_peripheral_status_start(void)
{
    k_work_schedule(&status_work, K_NO_WAIT);
}
