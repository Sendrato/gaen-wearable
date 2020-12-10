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

#include <settings/settings.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#include "ct.h"
#include "ui.h"
#include "ct_app_en.h"
#include "ct_app_enc.h"

#include "ct_crypto.h"
#include "ct_db.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(ct_main, LOG_LEVEL_INF);

static ct_app_id_t _app = CT_APP_MAIN;

void btn_callback(int btn, uint8_t clk)
{
    if(clk == UI_BTN_LONGPRESS) {
        LOG_DBG("long press");
        // Stop current active app.
        // => start of new app is handled in app-callback.

        if (_app == CT_APP_ENC) {
            ct_app_enc_stop();
        } else if (_app == CT_APP_EN) {
            ct_app_en_stop();
        } else {
            LOG_ERR("invalid app is active");
        }

    } else {
        LOG_DBG("%d clicks", clk);
    }
}

void ct_app_event(ct_app_id_t app, ct_event_t event)
{
    if ( event == CT_EVENT_BATTERY_EMPTY ) {
        ui_led_blink(UI_LED_RED, 3);
    }

    if(app == CT_APP_ENC) {
        // When ENC is stopped, start EN.
        if(event == CT_EVENT_STOP) {
            _app = CT_APP_EN;
            ct_app_en_start();
            ui_led_off(UI_LED_GREEN);
            ui_haptic_blink(1);
        }

    } else if(app == CT_APP_EN) {

        switch ( event ) {
            // When EN is stopped, start ENC.
            case CT_EVENT_STOP:
                _app = CT_APP_ENC;
                ct_app_enc_start();
                ui_led_blink(UI_LED_GREEN, UI_BLINK_INFINITE);
                ui_haptic_blink(5);
                break;

            case CT_EVENT_START_SCAN:
                ui_led_blink(UI_LED_GREEN, 1);
                ui_led_blink(UI_LED_RED  , 1);
                break;

            case CT_EVENT_NEW_RPI:
                ui_led_blink(UI_LED_GREEN, 2);
                ui_led_blink(UI_LED_RED  , 2);
                break;

            case CT_EVENT_INVALID_CLOCK:
                ui_led_blink(UI_LED_RED, 1);
                ui_haptic_blink(1);
                break;

            case CT_EVENT_ENOMEM:
                ui_led_blink(UI_LED_BLUE, 3);
                ui_haptic_blink(2);
                break;

            default:
                break;
        }
    }
}

void main(void)
{
	int err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return;
	}

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    printk("Contact Tracing Wearable\n");

    ui_init();
    ui_btn_set_callback(btn_callback);

    ct_crypto_init();
    ct_db_init();

    ct_app_en_init();
    ct_app_enc_init();

    //Start GAEN stack.
    _app = CT_APP_EN;
    ct_app_en_start();

    do {
        k_sleep(K_FOREVER);
    } while (1);
}
