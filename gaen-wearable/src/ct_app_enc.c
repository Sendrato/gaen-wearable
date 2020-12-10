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

#include <zephyr/types.h>
#include <stddef.h>
#include <stdio.h>

#include <sys/printk.h>
#include <sys/util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <settings/settings.h>

#include "ct.h"
#include "ct_app_enc.h"
#include "ct_app_state.h"
#include "ct_settings.h"
#include "ct_db.h"
#include "ct_crypto.h"

#include "ctsa.h"
#include "disa.h"
#include "basa.h"

#include "battery.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(app_enc, LOG_LEVEL_INF);

#if !defined(CONFIG_BT_SMP)
#error "BLE SMP IS MANDATORY FOR THIS APPLICATION"
#endif

// TEK structure which is communicated with BLE offloading.
typedef struct __attribute__((__packed__)){
    uint8_t tek[TEK_SIZE];
    uint32_t ival;
} bt_tek_t;

// RPI structure which is communicated with BLE offloading.
typedef struct __attribute__((__packed__)) {
    uint8_t rpi[RPI_SIZE];
    uint8_t aem[AEM_SIZE];
    uint32_t ival_last;
    int8_t rssi;
    uint8_t cnt;
} bt_rpi_t;

// App states and worker queue
static app_state_t _enc_state = APP_STATE_UNDEF; // active/stopped indicator
static struct k_delayed_work _enc_state_work;
// setup&start enc_app worker
static void app_enc_state_start(struct k_work *work);
// terminate enc-app
static void app_enc_state_finish(struct k_work *work);

// >> sends ping from phone to wearable, responds with the same as pong
#define CMD_PING             (0x00)

// Data Management
// >> no payload
#define CMD_CLEAR_DB_ALL     (0x01)
#define CMD_CLEAR_DB_RPI     (0x02)
#define CMD_CLEAR_DB_TEK     (0x03)
// TEK/RPI data
#define CMD_SET_RPI_IDX      (0x04)
#define CMD_GET_RPI_IDX      (0x05)
#define CMD_SET_TEK_IDX      (0x06)
#define CMD_GET_TEK_IDX      (0x07)

// Bluetooth settings
// >> 4 bytes, unsigned, milliseconds
#define CMD_SET_ADV_PERIOD   (0x10)
#define CMD_GET_ADV_PERIOD   (0x11)
// >> 4 bytes, unsigned, milliseconds
#define CMD_SET_SCAN_PERIOD  (0x12)
#define CMD_GET_SCAN_PERIOD  (0x13)
// >> 2 bytes, unsigned, 0.625 millisecond steps
#define CMD_SET_ADV_IVAL_MIN (0x14)
#define CMD_GET_ADV_IVAL_MIN (0x15)
// >> 2 bytes, unsigned, 0.625 millisecond steps
#define CMD_SET_ADV_IVAL_MAX (0x16)
#define CMD_GET_ADV_IVAL_MAX (0x17)

// EN settings
#define CMD_SET_TEK_IVAL     (0x20)
#define CMD_GET_TEK_IVAL     (0x21)
#define CMD_SET_TEK_PERIOD   (0x22)
#define CMD_GET_TEK_PERIOD   (0x23)

// Device name
#define CMD_SET_DEVICENAME   (0x30)
#define CMD_GET_DEVICENAME   (0x31)

// Status masks.
#define CMD_MASK_OK          (0x80)
#define CMD_MASK_ERR         (0x40)

/************* BT CONNECTION ***************/

// connection structure to track amount of RPI/TEKs which have been read
typedef struct {
    struct bt_conn *conn;
    uint16_t idx_rpi;
    uint16_t idx_tek;
} enc_conn_t;

static enc_conn_t _enc_bt_conn[CONFIG_BT_MAX_PAIRED];

static int enc_bt_conn_get(struct bt_conn *conn, enc_conn_t** enc_conn)
{
    if ((!conn) || (!enc_conn))
        return -EINVAL;

    for(int i=0;i<CONFIG_BT_MAX_PAIRED;i++) {
        if (_enc_bt_conn[i].conn == conn) {
            *enc_conn = &_enc_bt_conn[i];
            return 0;
        }
    }

    // not found
    return -EINVAL;
}

static int enc_bt_conn_clear(struct bt_conn *conn)
{
    if (!conn)
        return -EINVAL;

    enc_conn_t *enc_conn;
    if (enc_bt_conn_get(conn,&enc_conn) != 0)
        return -EINVAL;

    memset(enc_conn,0,sizeof(enc_conn_t));
    return 0;
}

static int enc_bt_conn_new(enc_conn_t** enc_conn)
{
    for(int i=0;i<CONFIG_BT_MAX_PAIRED;i++) {
        if (_enc_bt_conn[i].conn == NULL) {
            *enc_conn = &_enc_bt_conn[i];
            return 0;
        }
    }
    return -ENOMEM;
}

/************* BT DEFINITIONS ***************/

// notification-function enabled by connected device
static volatile bool _enc_bt_notify_enabled = false;

static ssize_t enc_bt_cmd_on_receive(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  const void *buf,
			  uint16_t len,
			  uint16_t offset,
			  uint8_t flags);

static ssize_t enc_bt_rpi_on_read(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  void *buf,
			  uint16_t len,
			  uint16_t offset);

static ssize_t enc_bt_tek_on_read(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  void *buf,
			  uint16_t len,
			  uint16_t offset);

static void enc_bt_resp_ccc_cfg_changed(const struct bt_gatt_attr *attr,
               uint16_t value);

#define ENC_BT_UUID_SERVICE_PRIMARY \
	BT_UUID_128_ENCODE(0xb3c04e98, 0x82b5, 0x4587, 0x84b6, 0x6179a66a079f)
#define ENC_BT_UUID_CMD_CHAR \
	BT_UUID_128_ENCODE(0xb3c04e99, 0x82b5, 0x4587, 0x84b6, 0x6179a66a079f)
#define ENC_BT_UUID_RESP_CHAR \
	BT_UUID_128_ENCODE(0xb3c04e9a, 0x82b5, 0x4587, 0x84b6, 0x6179a66a079f)
#define ENC_BT_UUID_READ_RPI_CHAR \
	BT_UUID_128_ENCODE(0xb3c04e9b, 0x82b5, 0x4587, 0x84b6, 0x6179a66a079f)
#define ENC_BT_UUID_READ_TEK_CHAR \
	BT_UUID_128_ENCODE(0xb3c04e9c, 0x82b5, 0x4587, 0x84b6, 0x6179a66a079f)


#define ENC_BT_UUID_SERVICE    BT_UUID_DECLARE_128(ENC_BT_UUID_SERVICE_PRIMARY)
#define ENC_BT_UUID_CMD        BT_UUID_DECLARE_128(ENC_BT_UUID_CMD_CHAR)
#define ENC_BT_UUID_RESP       BT_UUID_DECLARE_128(ENC_BT_UUID_RESP_CHAR)
#define ENC_BT_UUID_READ_RPI   BT_UUID_DECLARE_128(ENC_BT_UUID_READ_RPI_CHAR)
#define ENC_BT_UUID_READ_TEK   BT_UUID_DECLARE_128(ENC_BT_UUID_READ_TEK_CHAR)

static const struct bt_data _enc_bt_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, ENC_BT_UUID_SERVICE_PRIMARY),
};

static const struct bt_data _enc_bt_sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
        0x0a, 0x18,   // Device Information Service
        0x0f, 0x18,   // Battery Service
        0x05, 0x18),  // Current Time Service
	BT_DATA(BT_DATA_NAME_COMPLETE, ct_priv.device_name, sizeof(ct_priv.device_name)),
};

static struct bt_gatt_attr _enc_bt_service_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(ENC_BT_UUID_SERVICE),
	BT_GATT_CHARACTERISTIC(ENC_BT_UUID_CMD,
		BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_WRITE_AUTHEN,
		NULL, enc_bt_cmd_on_receive, NULL),
	BT_GATT_CHARACTERISTIC(ENC_BT_UUID_RESP,
        BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_NONE,
        NULL, NULL, NULL),
    BT_GATT_CCC(enc_bt_resp_ccc_cfg_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(ENC_BT_UUID_READ_RPI,
        BT_GATT_CHRC_READ,
		BT_GATT_PERM_READ_AUTHEN,
        enc_bt_rpi_on_read, NULL, NULL),
    BT_GATT_CHARACTERISTIC(ENC_BT_UUID_READ_TEK,
        BT_GATT_CHRC_READ,
		BT_GATT_PERM_READ_AUTHEN,
        enc_bt_tek_on_read, NULL, NULL)
};

static struct bt_gatt_service _enc_bt_service =
		    BT_GATT_SERVICE(_enc_bt_service_attrs);

/************* BT NOTIFICATION ***************/

static void enc_bt_resp_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                uint16_t value)
{
	ARG_UNUSED(attr);

	_enc_bt_notify_enabled = (value == BT_GATT_CCC_NOTIFY);

	LOG_INF("ENC APP Notifications %s",
        _enc_bt_notify_enabled ? log_strdup("enabled") : log_strdup("disabled"));
}

static void enc_app_notify(struct bt_conn *conn, uint8_t mask,
                const uint8_t *data, uint8_t data_len)
{
    if (_enc_bt_notify_enabled == false) {
        LOG_ERR("user did not enable notification");
        return;
    }

    enc_conn_t* enc_conn;
    if (enc_bt_conn_get(conn, &enc_conn) != 0) {
        LOG_ERR("Unknown connection");
		return;
	}

    uint8_t cmd_idx   = 0;

    // response array ==> 30 bytes is only to ensure we have enough data.
    //  It is a quick fix and works as long as "data_len" will not exceed "30".
    uint8_t resp[30] = {0};
    uint8_t resp_len = 0;

    // in most cases we want to copy data to response and signal if all
    //  is ok or not. The next "if" and "switch" - statements will correct
    //  the response where needed.
    //  This is just a lazy (but functioning) approach.
    memcpy(resp, data, data_len);
    resp_len = data_len;

    //Upon error: mask CMD with error and responde with provided data
    if (mask == CMD_MASK_ERR) {
        resp[cmd_idx] |= CMD_MASK_ERR;

    } else if (mask == CMD_MASK_OK) {
        resp[cmd_idx] |= CMD_MASK_OK;

        //different value representation
        uint8_t  *resp_u8  = (uint8_t *) &resp[1];
        //int8_t   *resp_s8  = (int8_t  *) &resp[1];
        uint16_t *resp_u16 = (uint16_t*) &resp[1];
        //int16_t *resp_s16 = (int16_t*) &resp[1];
        uint32_t *resp_u32 = (uint32_t*) &resp[1];
        //int32_t *resp_s32 = (int32_t*) &resp[1];

        switch ( data[cmd_idx] ) {
            case CMD_PING:
                break;

            // Data Management : database clearing
            case CMD_CLEAR_DB_TEK:
            case CMD_CLEAR_DB_RPI:
            case CMD_CLEAR_DB_ALL:
                break;

            // Data Management : RPI
            case CMD_SET_RPI_IDX:
            case CMD_GET_RPI_IDX:
                *resp_u16 = enc_conn->idx_rpi;
                resp_len  = 2 + 1;
                break;

            // Data Management : TEK
            case CMD_SET_TEK_IDX:
            case CMD_GET_TEK_IDX:
                *resp_u16 = enc_conn->idx_tek;
                resp_len  = 2 + 1;
                break;

            // Bluetooth settings : Advertisement period [ms]
            case CMD_SET_ADV_PERIOD:
            case CMD_GET_ADV_PERIOD:
                *resp_u32 = ct_priv.adv_period;
                resp_len  = 4 + 1;
                break;

            // Bluetooth settings : Scan period [ms]
            case CMD_SET_SCAN_PERIOD:
            case CMD_GET_SCAN_PERIOD:
                *resp_u32 = ct_priv.scan_period;
                resp_len  = 4 + 1;
                break;

            // Bluetooth settings : Minimum Advertisement Interval [0.625 ms]
            case CMD_SET_ADV_IVAL_MIN:
            case CMD_GET_ADV_IVAL_MIN:
                *resp_u16 = ct_priv.adv_ival_min;
                resp_len  = 2 + 1;
                break;

            // Bluetooth settings : Maximum Advertisement Interval [0.625 ms]
            case CMD_SET_ADV_IVAL_MAX:
            case CMD_GET_ADV_IVAL_MAX:
                *resp_u16 = ct_priv.adv_ival_max;
                resp_len  = 2 + 1;
                break;

            // GAEN : TEK rolling Interval
            case CMD_SET_TEK_IVAL:
            case CMD_GET_TEK_IVAL:
                *resp_u32 = ct_priv.tek_rolling_interval;
                resp_len  = 4 + 1;
                break;

            // GAEN : TEK rolling period
            case CMD_SET_TEK_PERIOD:
            case CMD_GET_TEK_PERIOD:
                *resp_u32 = ct_priv.tek_rolling_period;
                resp_len  = 4 + 1;
                break;

            // System : Device name
            case CMD_SET_DEVICENAME:
            case CMD_GET_DEVICENAME:
                memcpy(resp_u8, ct_priv.device_name, sizeof(ct_priv.device_name));
                resp_u8[sizeof(ct_priv.device_name)] = '\0';
                resp_len  = sizeof(ct_priv.device_name) + 1;
                LOG_INF("DeviceName: %s/%s", resp_u8, ct_priv.device_name);
                break;

            // unknown command..
            default:
                LOG_ERR("unknown cmd: %02x",data[cmd_idx]);
                resp[cmd_idx] &= ~CMD_MASK_OK;
                resp[cmd_idx] |=  CMD_MASK_ERR;
                break;
        }

    } else {
        LOG_ERR("unknown mask: %02x",mask);
        resp[cmd_idx] |= CMD_MASK_ERR;
    }

    bt_gatt_notify(NULL, &_enc_bt_service.attrs[2], resp, resp_len);
}



/************* BT READ RPI AND TEK  ***************/

static ssize_t enc_bt_rpi_on_read(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  void *b,
			  uint16_t buf_len,
			  uint16_t offset)
{
	LOG_DBG("Attribute read, handle: %u, conn: %p", attr->handle, conn );
    uint8_t *buf = (uint8_t*)b;

    // Does connection exists? approved by user
    enc_conn_t* enc_conn;
    if (enc_bt_conn_get(conn, &enc_conn) != 0) {
        LOG_ERR("Unknown connection");
		return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
	}

	if (0) { // not approved by user
		return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
	}

    // We need to creat a byte-stream of consequtive RPIs.
    //  As we cannot push all data at once, we need to recompute on each request
    //  which (part of which) RPI needs to be copied to the provided buffer.

    // 1) Compute number of RPIs in DB
    uint16_t cnt;
    ct_db_rpi_get_cnt(&cnt);

    // Do we have data?
    if (cnt == 0) {
        return 0;
    }

    // 2) Total amount fo data to be transferred.
    int value_len = cnt * sizeof(bt_rpi_t);

    LOG_DBG(">> cnt:%d, val_len:%d\n", cnt, value_len );

    // 3) Check if request/offset is valid,.
	if (offset > value_len) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

    // max read-limit in BLE = 512 bytes
    const uint16_t limit     = 512;
    const uint16_t header    = 6;
    // number of 'full' readouts we can do. (floored!)
    const uint8_t  readouts  = limit/buf_len;
    // number of bytes we can transfer in these readouts
    const uint16_t max_bytes = readouts*buf_len;
    // number of RPI's we can read in these readouts (floored!)
    const uint16_t max_rpis  = (max_bytes - header) / sizeof(bt_rpi_t);

    // remaining number of RPI's which still need to be transferred
    // => when no RPI's remain, start over again
    if (enc_conn->idx_rpi >= cnt) {
        enc_conn->idx_rpi = 0;
    }
    uint16_t rem_rpis  = cnt - enc_conn->idx_rpi;

    // number of RPI's which are read in read-limit
    // (limited by remaining number of RPI's)
    const uint16_t read_rpis = MIN(max_rpis,rem_rpis);

    // 4) Compute starting point of RPI [in current read-block] we need to fetch.
    // ==> rpi_idx = byte in RPI which at which we need to start
    // ==> rpi_num = RPI number in database at which we need to start.
    if (offset > 0) {
        offset -= header;
    }
    uint16_t rpi_idx = offset % sizeof(bt_rpi_t);
    uint16_t rpi_num = (offset - rpi_idx) / sizeof(bt_rpi_t);

    // 5) Compute number of bytes which will be transferred [in this read-out].
    const int read_len = MIN(buf_len, (read_rpis*sizeof(bt_rpi_t)) - offset);

    LOG_DBG(">> lim:%d ro:%d mx-b:%d mx-rpi:%d rem-rpi:%d rd-rpi:%d\n",
                limit, readouts, max_bytes, max_rpis, rem_rpis, read_rpis);
    LOG_DBG(">> idx:%d num:%d read:%d\n", rpi_idx, rpi_num, read_len);

    bt_rpi_t bt_rpi;
    int i = 0;

    //6) For first read of block we need to add header!
    if (offset == 0) {
        i += header;
        // Starting index of first RPI
        memcpy( &buf[0], (uint8_t*)&enc_conn->idx_rpi, 2);
        // Number of RPI's in this readout
        memcpy( &buf[2], (uint8_t*)&read_rpis, 2);
        // Remaining RPI's (after current readout is completed)
        rem_rpis -= read_rpis;
        memcpy( &buf[4], (uint8_t*)&rem_rpis, 2);
    }

    // 7) Copy RPI data..
    do {
        uint16_t len;

        // Get RPI
        ct_db_rpi_get(rpi_num + enc_conn->idx_rpi, bt_rpi.rpi, bt_rpi.aem,
                        &bt_rpi.rssi, &bt_rpi.cnt, &bt_rpi.ival_last);

        // Compute and Copy RPI-remainer
        len = MIN(sizeof(bt_rpi_t) - rpi_idx, read_len - i);
        uint8_t *p = (uint8_t*) &bt_rpi;
        memcpy( &buf[i], &p[rpi_idx], len);
        i+=len;      // Amount of data copied..
        rpi_idx = 0; // Start at first byte for next RPI
        rpi_num++;   // Copy next RPI
    } while (i<read_len);

    if (read_rpis == rpi_num)
        enc_conn->idx_rpi += rpi_num;

    LOG_INF("RPI [off:%d buf:%d db:%d][read:%d==%d][%d]\n", offset, buf_len,
                    value_len, i, read_len, enc_conn->idx_rpi);

    // return amount of data which has been pushed to the provided buffer.
	return read_len;
}



static ssize_t enc_bt_tek_on_read(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  void *b,
			  uint16_t buf_len,
			  uint16_t offset)
{
	LOG_DBG("Attribute read, handle: %u, conn: %p", attr->handle, conn);
    uint8_t *buf = (uint8_t*)b;

    // Does connection exists? approved by user
    enc_conn_t* enc_conn;
    if (enc_bt_conn_get(conn, &enc_conn) != 0) {
        LOG_ERR("Unknown connection");
		return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
	}

	if (0) { // not approved by user
		return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
	}

    // We need to creat a byte-stream of consequtive TEKs + RollingInterval
    //  As we cannot push all data at once, we need to recompute on each request
    //  which (part of which) TEK needs to be copied to the provided buffer.

    // 1) Compute number of TEKs in DB
    uint16_t cnt;
    ct_db_tek_get_cnt(&cnt);

    // Do we have data?
    if (cnt == 0) {
        return 0;
    }

    // 2) Total amount fo data to be transferred.
    int value_len = cnt * sizeof(bt_tek_t);

    LOG_DBG(">> cnt:%d, val_len:%d", cnt, value_len );

    // 3) Check if request/offset is valid,.
	if (offset > value_len) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

    // max read-limit in BLE = 512 bytes
    const uint16_t limit     = 512;
    const uint16_t header    = 6;
    // number of 'full' readouts we can do. (floored!)
    const uint8_t  readouts  = limit/buf_len;
    // number of bytes we can transfer in these readouts
    const uint16_t max_bytes = readouts*buf_len;
    // number of TEK's we can read in these readouts (floored!)
    const uint16_t max_teks  = (max_bytes - header) / sizeof(bt_tek_t);

    // remaining number of RPI's which still need to be transferred
    // => when no RPI's remain, start over again
    if (enc_conn->idx_tek >= cnt) {
        enc_conn->idx_tek = 0;
    }
    uint16_t rem_teks  = cnt - enc_conn->idx_tek;

    // number of TEK's which are read in read-limit
    // (limited by remaining number of TEK's)
    const uint16_t read_teks = MIN(max_teks,rem_teks);

    // 4) Compute starting point of TEK we need to fetch.
    // ==> tek_idx = byte in TEK which at which we need to start
    // ==> tek_num = tek number in database at which we need to start.
    uint16_t tek_idx = offset % sizeof(bt_tek_t);
    uint16_t tek_num = (offset - tek_idx) / sizeof(bt_tek_t);

    // 5) Compute number of bytes which will be transferred.
    const int read_len = MIN(buf_len, (read_teks*sizeof(bt_tek_t)) - offset);

    bt_tek_t bt_tek;
    int i = 0;

    //6) For first read of block we need to add header!
    if (offset == 0) {
        i += header;
        // Starting index of first TEK
        memcpy( &buf[0], (uint8_t*)&enc_conn->idx_tek, 2);
        // Number of TEK's in this readout
        memcpy( &buf[2], (uint8_t*)&read_teks, 2);
        // Remaining TEK's (after current readout is completed)
        rem_teks -= read_teks;
        memcpy( &buf[4], (uint8_t*)&rem_teks, 2);
    }

    // 7) Copy TEK data..
    do {
        uint16_t len;

        // Get TEK
        ct_db_tek_get(tek_num + enc_conn->idx_tek, bt_tek.tek, &bt_tek.ival);

        // Compute and Copy TEK-remainder
        len = MIN(sizeof(bt_tek_t) - tek_idx, read_len - i);
        uint8_t *p = (uint8_t*) &bt_tek;
        memcpy( &buf[i], &p[tek_idx], len);
        i+=len;      // Amount of data copied..
        tek_idx = 0; // Start at first byte for next TEK
        tek_num++;   // Copy next TEK
    } while (i<read_len);

    if (read_teks == tek_num)
        enc_conn->idx_tek += tek_num;

    // return amount of data which has been pushed to the provided buffer.
    LOG_DBG("TEK [off:%d buf:%d db:%d][read:%d==%d][%d]\n", offset, buf_len,
                    value_len, i, read_len, enc_conn->idx_tek);

	return read_len;
}

/************* BT CMD HANDLING  ***************/


static ssize_t enc_bt_cmd_on_receive(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  const void *buf,
			  uint16_t len,
			  uint16_t offset,
			  uint8_t flags)
{
	LOG_DBG("Received cmd data, handle %d, conn %p", attr->handle, conn);

    // Does connection exists? approved by user
    enc_conn_t* enc_conn;
    if (enc_bt_conn_get(conn, &enc_conn) != 0) {
        LOG_ERR("Unknown connection");
		return len;
	}

    if(len < 1) {
        LOG_ERR("ENC_APP: Empty command received");
        return len;
    }

    const uint8_t *b = (uint8_t*) buf;

    //different value representation
    uint8_t  *buf_u8  = (uint8_t *) &b[1];
    //int8_t  *buf_s8  = (int8_t *) &b[1];
    uint16_t *buf_u16 = (uint16_t*) &b[1];
    //int16_t *buf_s16 = (int16_t*) &b[1];
    uint32_t *buf_u32 = (uint32_t*) &b[1];
    //int32_t *buf_s32 = (int32_t*) &b[1];

    switch(b[0]) {
        case CMD_PING:
        case CMD_GET_RPI_IDX:
        case CMD_GET_TEK_IDX:
        case CMD_GET_ADV_PERIOD:
        case CMD_GET_SCAN_PERIOD:
        case CMD_GET_ADV_IVAL_MIN:
        case CMD_GET_ADV_IVAL_MAX:
        case CMD_GET_TEK_IVAL:
        case CMD_GET_TEK_PERIOD:
        case CMD_GET_DEVICENAME:
        {
            LOG_DBG("CMD_GET: %02x",b[0]);
            enc_app_notify(conn, CMD_MASK_OK, buf, len);
            break;
        }

        case CMD_CLEAR_DB_ALL:
        {
            LOG_DBG("CMD_CLEAR_DB_ALL, %d", len);
            if(len != 1) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                if (ct_db_clear() != 0) {
                    enc_app_notify(conn, CMD_MASK_ERR, buf, len);
                } else {
                    enc_app_notify(conn, CMD_MASK_OK, buf, len);
                }
            }
            break;
        }

        case CMD_CLEAR_DB_RPI:
        {
            LOG_DBG("CMD_CLEAR_DB_RPI, %d", len);
            if(len != 1) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                if (ct_db_rpi_clear() != 0) {
                    enc_app_notify(conn, CMD_MASK_ERR, buf, len);
                } else {
                    enc_app_notify(conn, CMD_MASK_OK, buf, len);
                }
            }
            break;
        }

        case CMD_CLEAR_DB_TEK:
        {
            LOG_DBG("CMD_CLEAR_DB_TEK, %d", len);
            if(len != 1) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                if (ct_db_tek_clear() != 0) {
                    enc_app_notify(conn, CMD_MASK_ERR, buf, len);
                } else {
                    enc_app_notify(conn, CMD_MASK_OK, buf, len);
                }
            }
            break;
        }

        case CMD_SET_RPI_IDX:
        {
            LOG_DBG("CMD_SET_RPI_IDX");
            if(len != 3) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                enc_conn->idx_rpi = *buf_u16;
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        case CMD_SET_TEK_IDX:
        {
            LOG_DBG("CMD_SET_TEK_IDX");
            if(len != 3) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                enc_conn->idx_tek = *buf_u16;
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        case CMD_SET_ADV_PERIOD:
        {
            LOG_DBG("CMD_SET_ADV_PERIOD");
            if(len != 5) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                ct_priv.adv_period = *buf_u32;
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        case CMD_SET_SCAN_PERIOD:
        {
            LOG_DBG("CMD_SET_SCAN_PERIOD");
            if(len != 5) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                ct_priv.scan_period = *buf_u32;
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        case CMD_SET_ADV_IVAL_MIN:
        {
            LOG_DBG("CMD_SET_ADV_IVAL_MIN");
            if(len != 3) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                ct_priv.adv_ival_min = *buf_u16;
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        case CMD_SET_ADV_IVAL_MAX:
        {
            LOG_DBG("CMD_SET_ADV_IVAL_MAX");
            if(len != 3) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                ct_priv.adv_ival_max = *buf_u16;
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        case CMD_SET_TEK_IVAL:
        {
            LOG_DBG("CMD_SET_TEK_IVAL");
            if(len != 5) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                ct_priv.tek_rolling_interval = *buf_u32;
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        case CMD_SET_TEK_PERIOD:
        {
            LOG_DBG("CMD_SET_TEK_PERIOD");
            if(len != 5) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                ct_priv.tek_rolling_period = *buf_u32;
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        case CMD_SET_DEVICENAME:
        {
            LOG_DBG("CMD_SET_DEVICENAME");
            if(len != (sizeof(ct_priv.device_name) + 1 )) {
                enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            } else {
                memcpy(ct_priv.device_name, buf_u8, sizeof(ct_priv.device_name));
                enc_app_notify(conn, CMD_MASK_OK, buf, len);
            }
            break;
        }

        default:
            LOG_WRN("unknown CMD received");
            enc_app_notify(conn, CMD_MASK_ERR, buf, len);
            break;
    }

	return len;
}

/************* BT SERVICES AND SETUP  ***************/

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("enc_app connection failed (err %u)", err);
	} else {
		LOG_INF("enc_app connected");

        enc_conn_t* enc_conn;
        if (enc_bt_conn_new(&enc_conn) != 0) {
            // can't allocate new connection.
            bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
            return;
        }

        enc_conn->conn    = bt_conn_ref(conn);
        enc_conn->idx_rpi = 0;
        enc_conn->idx_tek = 0;

        // clear scheduled states ==> BLE disconnect will handle next steps.
        APP_STATE_CLEAR(&_enc_state_work);

        ct_app_event(CT_APP_ENC, CT_EVENT_CONNECTED);

        if (bt_conn_set_security(conn, BT_SECURITY_L4 | BT_SECURITY_FORCE_PAIR)) {
            LOG_ERR("Failed to set security\n");
        }
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_DBG("Disconnected (reason %u)", reason);

    enc_conn_t* enc_conn;
    if (enc_bt_conn_get(conn, &enc_conn) == 0) {
        bt_conn_unref(enc_conn->conn);
        enc_bt_conn_clear(conn);
    } else {
        LOG_ERR("Secondary connection is disconnected ?!?");
    }

    // extend ENC-APP timeout so there is more time to (re)connect
    APP_STATE_EXTEND(&_enc_state_work,K_SECONDS(30));

    ct_app_event(CT_APP_ENC, CT_EVENT_DISCONNECTED);

}


static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

	LOG_INF("Identity resolved %s -> %s\n",
                    log_strdup(addr_rpa), log_strdup(addr_identity));
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Security changed: %s level %u", log_strdup(addr), level);
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u", log_strdup(addr), passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", log_strdup(addr));
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing Complete\n");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("Pairing Failed (%d). Disconnecting.\n", reason);
	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);

    APP_STATE_EXTEND(&_enc_state_work,K_SECONDS(30));
}

static struct bt_conn_auth_cb auth_cb_display = {
	.passkey_display = auth_passkey_display,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};


static struct bt_conn_cb _conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
#if defined(CONFIG_BT_SMP)
	.identity_resolved = identity_resolved,
	.security_changed = security_changed,
#endif
};


/************* ENC_APP STATES ***************/

static void app_enc_state_start(struct k_work *work)
{
    bt_le_scan_stop();
    bt_le_adv_stop();

    //clear notifcation flag
    _enc_bt_notify_enabled = false;

    LOG_INF("ENC APP start");

    // Sample battery.
    int batt_mV = battery_sample();
    LOG_DBG("BATT: %d [mV]", batt_mV);
    if(batt_mV >= 0) {
        int batt_pptt = battery_level_pptt(batt_mV, CT_BATT_TYPE);
        bt_gatt_basa_set_battery_level(batt_pptt / 100);
        LOG_DBG("BATT: %d [pptt]\n", batt_pptt);
    }

    // start config advertisement (connect-able)
    bt_gatt_service_register(&_enc_bt_service);
    bt_gatt_ctsa_start();
    bt_gatt_disa_start();
    bt_gatt_basa_start();

	int err = bt_le_adv_start(BT_LE_ADV_CONN,
                        _enc_bt_ad, ARRAY_SIZE(_enc_bt_ad),
                        _enc_bt_sd, ARRAY_SIZE(_enc_bt_sd));
	if (err) {
		LOG_ERR("Advertising failed to start for config (err %d)", err);
	}

    ct_app_event(CT_APP_ENC, CT_EVENT_START);

    APP_STATE_NEXT_WQ(app_enc_state_finish, K_SECONDS(30));

}


static void app_enc_state_finish(struct k_work *work)
{
    LOG_INF("ENC APP stop");
    _enc_state = APP_STATE_STOPPED;

    bt_le_adv_stop();
    bt_gatt_service_unregister(&_enc_bt_service);
    bt_gatt_ctsa_stop();
    bt_gatt_disa_stop();
    bt_gatt_basa_stop();

    //store any pending settings
    settings_save();

    ct_app_event(CT_APP_ENC, CT_EVENT_STOP);
}

/************* ENC_APP CTRL ***************/


int ct_app_enc_init(void)
{

    memset(_enc_bt_conn,0,sizeof(_enc_bt_conn));

    bt_gatt_ctsa_init();
    bt_gatt_disa_init();
    bt_gatt_basa_init();
    battery_init();

#if defined(CONFIG_BT_SMP)
#if defined(CONFIG_BT_FIXED_PASSKEY)
    bt_passkey_set(123456);
#endif
    bt_conn_auth_cb_register(&auth_cb_display);
#endif

    bt_conn_cb_register(&_conn_callbacks);

    return 0;
}

int ct_app_enc_start(void)
{
    _enc_state = APP_STATE_ACTIVE;

    //delayed start to allow other apps to close down (open) BT connections
    APP_STATE_NEXT(&_enc_state_work, app_enc_state_start, K_MSEC(2000));

    LOG_INF("ENC APP start");

    return 0;
}

int ct_app_enc_stop(void)
{
    _enc_state = APP_STATE_STOPPED;
    APP_STATE_NOW(&_enc_state_work, app_enc_state_finish);
    LOG_INF("ENC APP stop");

    return 0;
}
