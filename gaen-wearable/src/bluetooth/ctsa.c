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

#include "ctsa.h"

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/printk.h>
#include <sys/byteorder.h>
#include <zephyr.h>

#include <time.h>
#include <posix/time.h>
#include <posix/sys/time.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(bt_ctsa, LOG_LEVEL_INF);

static uint8_t _ct[10];
static uint8_t _ct_update;

static void ct_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);

    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

    LOG_INF("CTS Notifications %s", notif_enabled ? "enabled" : "disabled");
}

static ssize_t read_ct(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                void *buf, uint16_t len, uint16_t offset)
{
    char *value = attr->user_data;

    bt_gatt_ctsa_now2buf(value);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(_ct));
}

static ssize_t write_ct(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    struct timespec ts;

    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(_ct)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    bt_gatt_ctsa_now2buf(value);

    memcpy(value + offset, buf, len);

    bt_gatt_ctsa_buf2timespec(value, &ts);
    clock_settime(CLOCK_REALTIME, &ts);
    bt_gatt_ctsa_now2buf(value);

    _ct_update = 1U;

    return len;
}

#if defined(CONFIG_BT_SMP_SC_ONLY) && (CONFIG_BT_SMP_SC_ONLY==y)
#define BT_GATT_PERM_READ_LEVEL  BT_GATT_PERM_READ_AUTHEN
#define BT_GATT_PERM_WRITE_LEVEL BT_GATT_PERM_WRITE_AUTHEN
#else
#define BT_GATT_PERM_READ_LEVEL  BT_GATT_PERM_READ
#define BT_GATT_PERM_WRITE_LEVEL BT_GATT_PERM_WRITE_AUTHEN
#endif

/* Current Time Service Declaration */
static struct bt_gatt_attr _ctsa_service_attrs[] = {
    BT_GATT_PRIMARY_SERVICE(BT_UUID_CTS),
    BT_GATT_CHARACTERISTIC(BT_UUID_CTS_CURRENT_TIME, BT_GATT_CHRC_READ |
                BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
                BT_GATT_PERM_READ_LEVEL | BT_GATT_PERM_WRITE_LEVEL,
                read_ct, write_ct, _ct),
    BT_GATT_CCC(ct_ccc_cfg_changed, BT_GATT_PERM_READ_LEVEL | BT_GATT_PERM_WRITE_LEVEL),
};

static struct bt_gatt_service _ctsa_service =
                BT_GATT_SERVICE(_ctsa_service_attrs);


void bt_gatt_ctsa_buf2timespec(uint8_t *buf, struct timespec* ts)
{
    struct tm now_tm;

    memset(ts,0,sizeof(struct timespec));

    now_tm.tm_year = sys_le16_to_cpu(*((uint16_t*)&buf[0]));
    now_tm.tm_mon  = buf[2]; /* months starting from 1 */
    now_tm.tm_mday = buf[3]; /* day */
    now_tm.tm_hour = buf[4]; /* hours */
    now_tm.tm_min  = buf[5]; /* minutes */
    now_tm.tm_sec  = buf[6]; /* seconds */

    ts->tv_sec = mktime(&now_tm);

    LOG_HEXDUMP_DBG(buf,10, " >> now2time:");
    LOG_DBG(" >> data: %d / %d / %d - %d:%d:%d", now_tm.tm_year,
                                                 now_tm.tm_mon,
                                                 now_tm.tm_mday,
                                                 now_tm.tm_hour,
                                                 now_tm.tm_min,
                                                 now_tm.tm_sec);
    LOG_DBG(" >> time: %lld", ts->tv_sec);
}




void bt_gatt_ctsa_now2buf(uint8_t *buf)
{
    uint16_t year;

    struct timespec ts;
    struct tm now_tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &now_tm);

    year = sys_cpu_to_le16(now_tm.tm_year);
    memcpy(buf,  &year, 2);  /* The number of years since 1900 */
    buf[2] = now_tm.tm_mon;  /* month, range 0 to 11 */
    buf[3] = now_tm.tm_mday; /* day of the month, range 1 to 31 */
    buf[4] = now_tm.tm_hour; /* hours, range 0 to 23 */
    buf[5] = now_tm.tm_min;  /* minutes, range 0 to 59 */
    buf[6] = now_tm.tm_sec;  /* seconds, range 0 to 59 */

    /* 'Day of Week' part of 'Day Date Time' */
    buf[7] = (now_tm.tm_wday == 0) ? 7 : now_tm.tm_wday; /* day of week starting from 1 */

    /* 'Fractions 256 part of 'Exact Time 256' */
    buf[8] = (uint8_t)((int)((ts.tv_nsec / 1000) * 256)/1000000);

    /* Adjust reason */
    buf[9] = 0U; /* No update, change, etc */

    LOG_HEXDUMP_DBG(buf,10," >> now2buf");
    LOG_DBG(" >> data: %d / %d / %d - %d:%d:%d", now_tm.tm_year,
                                                 now_tm.tm_mon,
                                                 now_tm.tm_mday,
                                                 now_tm.tm_hour,
                                                 now_tm.tm_min,
                                                 now_tm.tm_sec);
    LOG_DBG(" >> time: %lld", ts.tv_sec);
}

int bt_gatt_ctsa_init(void)
{
    return 0;
}

int bt_gatt_ctsa_start(void)
{
    bt_gatt_service_register(&_ctsa_service);
    return 0;
}

int bt_gatt_ctsa_stop(void)
{
    bt_gatt_service_unregister(&_ctsa_service);
    return 0;
}

void bt_gatt_ctsa_notify(void)
{
    /* Current Time Service updates only when time is changed */
    if (!_ct_update) {
        return;
    }

    _ct_update = 0U;
    bt_gatt_notify(NULL, &_ctsa_service.attrs[1], &_ct, sizeof(_ct));
}
