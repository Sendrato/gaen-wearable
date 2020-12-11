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

#include "disa.h"

#include <zephyr/types.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zephyr.h>
#include <init.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <drivers/hwinfo.h>

#include "ct.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(bt_disa, LOG_LEVEL_INF);

static uint8_t _disa_model[]  = CT_INFO_MODEL;
static uint8_t _disa_manuf[]  = CT_INFO_MANUF;
static uint8_t _disa_fw_rev[] = CT_INFO_FWREV;
static uint8_t _disa_hw_rev[] = CONFIG_BOARD;
static uint8_t _disa_serial[25];

static ssize_t read_str(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                    attr->user_data, strlen(attr->user_data));
}

#if PROVIDE_BT_GATT_DIS_PNP
static ssize_t read_pnp_id(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                    &disa_pnp_id, sizeof(disa_pnp_id));
}
#endif

#if defined(CONFIG_BT_SMP_SC_ONLY) && (CONFIG_BT_SMP_SC_ONLY==y)
#define BT_GATT_PERM_READ_LEVEL  BT_GATT_PERM_READ_AUTHEN
#define BT_GATT_PERM_WRITE_LEVEL BT_GATT_PERM_WRITE_AUTHEN
#else
#define BT_GATT_PERM_READ_LEVEL  BT_GATT_PERM_READ
#define BT_GATT_PERM_WRITE_LEVEL BT_GATT_PERM_WRITE_AUTHEN
#endif

/* Device Information Service Declaration */
static struct bt_gatt_attr _disa_service_attrs[] = {
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DIS),

    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_MODEL_NUMBER,
                   BT_GATT_CHRC_READ, BT_GATT_PERM_READ_LEVEL,
                   read_str, NULL, _disa_model),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_MANUFACTURER_NAME,
                   BT_GATT_CHRC_READ, BT_GATT_PERM_READ_LEVEL,
                   read_str, NULL, _disa_manuf),
#if PROVIDE_BT_GATT_DIS_PNP
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_PNP_ID,
                   BT_GATT_CHRC_READ, BT_GATT_PERM_READ_LEVEL,
                   read_pnp_id, NULL, &disa_pnp_id),
#endif
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_SERIAL_NUMBER,
                   BT_GATT_CHRC_READ, BT_GATT_PERM_READ_LEVEL,
                   read_str, NULL, _disa_serial),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_FIRMWARE_REVISION,
                   BT_GATT_CHRC_READ, BT_GATT_PERM_READ_LEVEL,
                   read_str, NULL, _disa_fw_rev),
    BT_GATT_CHARACTERISTIC(BT_UUID_DIS_HARDWARE_REVISION,
                   BT_GATT_CHRC_READ, BT_GATT_PERM_READ_LEVEL,
                   read_str, NULL, _disa_hw_rev),
};

static struct bt_gatt_service _disa_service =
                BT_GATT_SERVICE(_disa_service_attrs);

int bt_gatt_disa_init(void)
{
    uint32_t hwid[3];
    hwinfo_get_device_id((uint8_t*)hwid, sizeof(hwid));

    snprintf(_disa_serial, sizeof(_disa_serial), "%08X%08X%08X",
                hwid[0], hwid[1], hwid[2]);
    return 0;
}

int bt_gatt_disa_start(void)
{
    bt_gatt_service_register(&_disa_service);
    return 0;
}

int bt_gatt_disa_stop(void)
{
    bt_gatt_service_unregister(&_disa_service);
    return 0;
}
