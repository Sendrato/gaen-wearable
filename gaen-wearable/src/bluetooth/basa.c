/*
 * This file is part of the Contact Tracing / GAEN Wearable distribution
 *        https://github.com/Sendrato/gaen-wearable.
 *
 * Copyright (c) 2020 Vincent van der Locht (https://www.synchronicit.nl/)
 *                    Hessel van der Molen  (https://sendrato.com/)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/agpl-3.0.txt>.
 */

#include "basa.h"

#include <errno.h>
#include <init.h>
#include <sys/__assert.h>
#include <stdbool.h>
#include <zephyr/types.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/uuid.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(bt_bas, LOG_LEVEL_INF);

static uint8_t _battery_level = 100U;

static void blvl_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                uint16_t value)
{
    ARG_UNUSED(attr);

    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

    LOG_INF("BAS Notifications %s", notif_enabled ? "enabled" : "disabled");
}

static ssize_t read_blvl(struct bt_conn *conn,
                const struct bt_gatt_attr *attr, void *buf,
                uint16_t len, uint16_t offset)
{
    uint8_t lvl8 = _battery_level;

    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                &lvl8, sizeof(lvl8));
}

#if defined(CONFIG_BT_SMP_SC_ONLY) && (CONFIG_BT_SMP_SC_ONLY==y)
#define BT_GATT_PERM_READ_LEVEL  BT_GATT_PERM_READ_AUTHEN
#define BT_GATT_PERM_WRITE_LEVEL BT_GATT_PERM_WRITE_AUTHEN
#else
#define BT_GATT_PERM_READ_LEVEL  BT_GATT_PERM_READ
#define BT_GATT_PERM_WRITE_LEVEL BT_GATT_PERM_WRITE_AUTHEN
#endif

static struct bt_gatt_attr _basa_service_attrs[] = {
    BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
    BT_GATT_CHARACTERISTIC(BT_UUID_BAS_BATTERY_LEVEL,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                BT_GATT_PERM_READ_LEVEL, read_blvl, NULL,
                &_battery_level),
    BT_GATT_CCC(blvl_ccc_cfg_changed,
                BT_GATT_PERM_READ_LEVEL | BT_GATT_PERM_WRITE_LEVEL),
};

static struct bt_gatt_service _basa_service =
                BT_GATT_SERVICE(_basa_service_attrs);


uint8_t bt_gatt_basa_get_battery_level(void)
{
    return _battery_level;
}

int bt_gatt_basa_set_battery_level(uint8_t level)
{
    int rc;

    if (level > 100U) {
        return -EINVAL;
    }

    _battery_level = level;

    rc = bt_gatt_notify(NULL, &_basa_service.attrs[1], &level, sizeof(level));

    return rc == -ENOTCONN ? 0 : rc;
}

int bt_gatt_basa_init(void)
{
    return 0;
}

int bt_gatt_basa_start(void)
{
    bt_gatt_service_register(&_basa_service);
    return 0;
}

int bt_gatt_basa_stop(void)
{
    bt_gatt_service_unregister(&_basa_service);
    return 0;
}
