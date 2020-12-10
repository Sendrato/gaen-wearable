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

#include <time.h>
#include <posix/time.h>
#include <posix/sys/time.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include "ct.h"
#include "ct_app_en.h"
#include "ct_app_state.h"
#include "ct_settings.h"
#include "ct_db.h"
#include "ct_crypto.h"

#include "battery.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(app_en, LOG_LEVEL_INF);

// App states and worker queue
static app_state_t _en_state = APP_STATE_UNDEF; // active/stopped indicator
static struct k_delayed_work _en_state_work;
// setup&start en_app worker
static void app_en_state_start(struct k_work *work);
// run adv-period and schedule scan-period
static void app_en_state_adv(struct k_work *work);
// start scan-period and schedule adv-period.
static void app_en_state_scan(struct k_work *work);

/************* BT PARAMS ***************/

static uint8_t _en_bt_service_data[22] = { 0x6f,0xfd,
				'T', 'E', 'S', 'T',
				'T', 'E', 'S', 'T',
				'T', 'E', 'S', 'T',
				'T', 'E', 'S', 'T',
                'A', 'E', 'M', 'D' };

static struct bt_data _en_bt_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, 0x1A),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x6F,0xFD ),
	BT_DATA(BT_DATA_SVC_DATA16, _en_bt_service_data, sizeof(_en_bt_service_data))
};

static struct bt_le_adv_param  _en_bt_adv_param;
static struct bt_le_scan_param _en_bt_scan_param;

/************ EN PARAMS *****************/

static uint8_t _en_key_rpik[RPIK_SIZE];
static uint8_t _en_key_rpi[RPI_SIZE];
static uint8_t _en_key_aemk[AEMK_SIZE];
static uint8_t _en_key_aem[AEM_SIZE];

// TEK should be updated every 24 hours.
//   This function updates TEK if required, so when called more frequent then
//   once every 24 hours, TEK will be updated properly.
//   As this function depends on a random number generator, TEK-update is guarded
//   by comparing ENIntervalNumber and the contents of the last TEK.
static void en_tek_update( uint32_t ival )
{
    uint8_t  last_tek[TEK_SIZE];
    uint32_t last_ival;
    uint8_t  curr_tek[TEK_SIZE];
    uint32_t curr_ival;

    // Fetch last tek from database
    int err = ct_db_tek_get_last(last_tek, &last_ival);

    // no item in database, so ensure "tek_last" is NULL
    if (err) {
        last_ival = 0;
        memset(last_tek, 0, TEK_SIZE);
    }

    // get aligned interval number of current TEKRollingPeriod and compute tek
    //  which responds with this period.
    curr_ival = ival;

    // Check if the aligned IntervalNumbers match. This indicates that the
    //   current timeframe is still within the last timeframe.
    // When the IntervalNumbers match, check if the last key is none-empty.
    //   if empty, we should generate a new key, if not, we should not update.
    if ((last_ival <= curr_ival) &&
            ((last_ival + ct_priv.tek_rolling_period) > curr_ival) )
        {
        for(int i=0;i<TEK_SIZE; i++) {
            if (last_tek[i] != 0) {
                return;
            }
        }
    }

    // Update keys if new tek needs to be created.
    ct_crypto_calc_tek(curr_tek);
    ct_db_tek_add(curr_tek, curr_ival);
    ct_crypto_calc_rpik(curr_tek, _en_key_rpik);
    ct_crypto_calc_aemk(curr_tek, _en_key_aemk);

    LOG_DBG(" >> @ ival %i ", curr_ival);
    LOG_HEXDUMP_DBG(curr_tek, TEK_SIZE, " >> New TEK:");
    LOG_HEXDUMP_DBG(_en_key_rpik, RPIK_SIZE, " >> New RPIK:");
    LOG_HEXDUMP_DBG(_en_key_aemk, AEMK_SIZE, " >> New AEMK:");

    ct_app_event(CT_APP_EN, CT_EVENT_NEW_TEK);
}


// RPI should be updated every 10 minutes or when TEK is updated.
//  RPI and AEM are recalculated every function call. No check or safe-guard
//  is added as these functions do not contain randomness, so providing the same
//  RPIK, AEMK, metadata and ENIntervalNumber, the output should also be the same.
static void en_rpi_update( uint32_t ival )
{
    int enin_j = ival;
    ct_crypto_calc_rpi(enin_j, _en_key_rpik, _en_key_rpi);
}

static void en_aem_update(void)
{
    uint8_t metadata[4];
    metadata[0] = 0x40; // version
    metadata[1] = 0x00; // tx_power == 0dBm
    metadata[2] = 0x00; // reserved
    metadata[3] = 0x00; // reserved

    // AEM calculation adjusts "RPI", as this is used as counter.
    // ==> Copy RPI to ensure this update will not propagate through this FW
    uint8_t rpi_ctr[RPI_SIZE];
    memcpy(rpi_ctr,_en_key_rpi,RPI_SIZE);
    ct_crypto_calc_aem(_en_key_aemk, rpi_ctr, metadata, _en_key_aem);
}

static void en_bt_scan_cb(const bt_addr_le_t *addr, int8_t rssi,
            uint8_t adv_type, struct net_buf_simple *ad)
{
    int ret;
    char rpi_str[80];

	if (adv_type != BT_GAP_ADV_TYPE_ADV_NONCONN_IND) {
		return;
	}

	while (ad->len > 1) {
		uint8_t len = net_buf_simple_pull_u8(ad);
		uint8_t type;

		// Check for early termination
		if (len == 0U) {
			LOG_DBG("AD len = 0");
            return;
		}

		if (len > ad->len) {
			LOG_DBG("AD malformed");
			return;
		}

		type = net_buf_simple_pull_u8(ad);

		if (type == BT_DATA_UUID16_ALL) {
			if(ad->data[0] == 0x6F && ad->data[1] == 0xfd) {
                LOG_INF("\tCT Service");
            }
		} else if (type == BT_DATA_SVC_DATA16) {
			if(ad->data[0] == 0x6F && ad->data[1] == 0xfd) {

                char le_addr[BT_ADDR_LE_STR_LEN];
                bt_addr_le_to_str(addr, le_addr, sizeof(le_addr));

                memset(rpi_str, 0, sizeof(rpi_str));
                for(int i = 0; i<20; i++) {
                    snprintf(&rpi_str[i*3], sizeof(rpi_str) - i*3,
                                " %02X", ad->data[i+2]);
                }
                LOG_INF("\tCT RPI %s @ %i [dB] from %s", log_strdup(rpi_str),
                                rssi, log_strdup(le_addr));

                // insert RPI into database
                ret = ct_db_rpi_add(&ad->data[2], &ad->data[2+RPI_SIZE], rssi,
                            ct_crypto_intervalNumber_now());
                if (ret == ENOMEM) {
                    ct_app_event(CT_APP_EN, CT_EVENT_ENOMEM);
                }

            }
		}

        net_buf_simple_pull(ad, len - 1);
	}
}

/************* EN_APP STATES ***************/

static void app_en_state_start(struct k_work *work)
{
    LOG_DBG("state: START");
    ct_app_event(CT_APP_EN, CT_EVENT_START);

    // Stop all BT activity
    bt_le_adv_stop();
    bt_le_scan_stop();

    // Setup advertisment data
    APP_STATE_NOW_WQ(app_en_state_adv);
}


static void app_en_state_adv(struct k_work *work)
{
    int err;
    LOG_DBG("state: ADV");
    ct_app_event(CT_APP_EN, CT_EVENT_START_ADV);

    // As the adv-state in the GAEN stack will run most often, check battery.
    static uint8_t batt_sample_cnt = 0;
    if (batt_sample_cnt++ > 10) {
        batt_sample_cnt = 0;
        int batt_mV = battery_sample();
        if(batt_mV >= 0) {
            int batt_percent = battery_level_pptt(batt_mV, CT_BATT_TYPE) / 100;
            if (batt_percent < 5)
                ct_app_event(CT_APP_EN, CT_EVENT_BATTERY_EMPTY);
        }
    }

    // Stop scanning activity
    bt_le_scan_stop();

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    // Ensure internal clock is set to a proper value as the EN depends
    // heavily on the concept of (a correct) time. Note that we cannot check
    // what the correct value should be, but we know that the value should be
    // aleast after 01-Jan-2020, 00:00 [epoch: 1577836800]
    if (now.tv_sec < 1577836800) {
        ct_app_event(CT_APP_EN, CT_EVENT_INVALID_CLOCK);
        APP_STATE_NEXT_WQ(app_en_state_adv, K_MSEC(ct_priv.adv_period));
        return;
    }

    uint8_t rpi_old[RPI_SIZE];
    memcpy(rpi_old, _en_key_rpi, RPI_SIZE);

    //Update advertisement data
    // => implicitly updates '_en_key_rpi' among others.

    uint32_t ival = ct_crypto_intervalNumber_now();
    // Update TEK. Function is safe-guared against over-use.
    // => should change every 24h
    en_tek_update( ival );
    // Update RPI and AEM. Functions are not safe-guarded, but as they do not
    //  use random generators, output is the same upon every call if the same
    //  input is provided.
    en_rpi_update( ival );
    en_aem_update();
    // Sent tick to db.
    ct_db_tick( ival );


    // Reset advertisment when RPI has changed
    if ( memcmp(rpi_old, _en_key_rpi, RPI_SIZE) != 0 ) {

        LOG_INF("@ival: %i", ct_crypto_intervalNumber_now());
        LOG_HEXDUMP_INF(_en_key_rpi, RPI_SIZE, " >> New RPI");
        LOG_HEXDUMP_INF(_en_key_aem, AEM_SIZE, " >> New AEM ");

        // Stop advertising so we can update all parameters
        err = bt_le_adv_stop();
        if (err) {
            LOG_ERR("Advertising failed to stop (err %d)", err);
        }

        for(int i = 0; i< RPI_SIZE; i++) {
            _en_bt_service_data[i+2] = _en_key_rpi[i];
        }

        for(int i = 0; i< AEM_SIZE; i++) {
            _en_bt_service_data[i+2+RPI_SIZE] = _en_key_aem[i];
        }

        // reset BLE ID
        bt_id_reset(_en_bt_adv_param.id, NULL, NULL);

        // Force the usage of the identity address.
        //  This address will change every time `bt_le_adv_start` is called.
        _en_bt_adv_param.options      = (BT_LE_ADV_OPT_USE_IDENTITY);

        // Update ADV interval.
        _en_bt_adv_param.interval_min = ct_priv.adv_ival_min;
        _en_bt_adv_param.interval_max = ct_priv.adv_ival_max;

        // Start advertising
        // ==> will generate a new mac address for the advertisement!
        err = bt_le_adv_start(&_en_bt_adv_param, _en_bt_ad,
                        ARRAY_SIZE(_en_bt_ad), NULL, 0);
        if (err) {
            LOG_ERR("Advertising failed to start (err %d)", err);
        }
    }

    APP_STATE_NEXT_WQ(app_en_state_scan, K_MSEC(ct_priv.adv_period));
}


static void app_en_state_scan(struct k_work *work)
{
    LOG_DBG("state: SCAN");
    ct_app_event(CT_APP_EN, CT_EVENT_START_SCAN);

    // Do not stop Advertisemens as this will update the BT mac-address.

    // Setup of scan-paramets.
    // ==> Scan passivly, i.e. do not request scan-responses.
    _en_bt_scan_param.type       = BT_HCI_LE_SCAN_PASSIVE;
    // ==> Ignore duplicate advertisements during a single scan period.
    _en_bt_scan_param.options    = BT_LE_SCAN_OPT_FILTER_DUPLICATE;
    _en_bt_scan_param.interval   = ct_priv.scan_ival;
    _en_bt_scan_param.window     = ct_priv.scan_window;

    // Start scanning..
    int err = bt_le_scan_start(&_en_bt_scan_param, en_bt_scan_cb);
    if (err) {
        LOG_ERR("Scanning failed to start (err %d)", err);
    }

    // Scanning ==> Advertising
    APP_STATE_NEXT_WQ(app_en_state_adv, K_MSEC(ct_priv.scan_period));
}


/************* EN_APP CTRL ***************/

int ct_app_en_init(void)
{
    bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = CONFIG_BT_ID_MAX;

	bt_id_get(addrs, &count);

    if(count<2) {
        _en_bt_adv_param.id = bt_id_create(NULL, NULL);
    } else {
        _en_bt_adv_param.id = 1;
    }

    return 0;
}

int ct_app_en_start(void)
{
    _en_state = APP_STATE_ACTIVE;
    //delayed start to allow other apps to close down (open) BT connections
    APP_STATE_NEXT(&_en_state_work, app_en_state_start, K_MSEC(2000));

    LOG_INF("EN APP start");

    return 0;
}

int ct_app_en_stop(void)
{
    _en_state = APP_STATE_STOPPED;
    APP_STATE_CLEAR(&_en_state_work);

    bt_le_scan_stop();
    bt_le_adv_stop();

    LOG_INF("EN APP stop");

    ct_app_event(CT_APP_EN, CT_EVENT_STOP);

    return 0;
}
