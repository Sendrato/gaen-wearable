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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <zephyr.h>
#include <device.h>

#include <settings/settings.h>
#include <drivers/hwinfo.h>

#include "ct.h"
#include "ct_settings.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(ct_settings, LOG_LEVEL_NONE);

/* application parameters to be stored / loaded */

struct ct_settings ct_priv;

int ct_settings_handle_get(const char *name, char *val, int val_len_max);
int ct_settings_handle_set(const char *name, size_t len,
                    settings_read_cb read_cb, void *cb_arg);
int ct_settings_handle_commit(void);
int ct_settings_handle_export(int (*cb)(const char *name,
			        const void *value, size_t val_len));

/* static subtree handler */
SETTINGS_STATIC_HANDLER_DEFINE(ct_settings_handler, "ct",
                    ct_settings_handle_get,
			        ct_settings_handle_set,
                    ct_settings_handle_commit,
			        ct_settings_handle_export);


#define CT_SETTINGS_HANDLE_GET(_x) ({\
	if (settings_name_steq(name, #_x, &next) && !next) { \
		if(val_len_max < sizeof(ct_priv._x)) \
            return -EINVAL; \
		memcpy(val, &ct_priv._x, sizeof(ct_priv._x)); \
		return sizeof(ct_priv._x); \
	} \
})


#define CT_SETTINGS_HANDLE_GET_ARR(_x) ({\
	if (settings_name_steq(name, #_x, &next) && !next) { \
		if(val_len_max < sizeof(ct_priv._x)) \
            return -EINVAL; \
		memcpy(val, ct_priv._x, sizeof(ct_priv._x)); \
		return sizeof(ct_priv._x); \
	} \
})

/* get the value from the runtime environment, to store it to flash */
int ct_settings_handle_get(const char *name, char *val, int val_len_max)
{
	const char *next;

    LOG_DBG("get:<ct>\n");

    CT_SETTINGS_HANDLE_GET(adv_period);
    CT_SETTINGS_HANDLE_GET(scan_period);

    CT_SETTINGS_HANDLE_GET(adv_ival_min);
    CT_SETTINGS_HANDLE_GET(adv_ival_max);

    CT_SETTINGS_HANDLE_GET(scan_ival);
    CT_SETTINGS_HANDLE_GET(scan_window);

    CT_SETTINGS_HANDLE_GET(tek_rolling_interval);
    CT_SETTINGS_HANDLE_GET(tek_rolling_period);

    CT_SETTINGS_HANDLE_GET_ARR(device_name);

	return -ENOENT;
}

#define CT_SETTINGS_HANDLE_SET(_x) ({\
		if (!strncmp(name,  #_x, name_len)) { \
			rc = read_cb(cb_arg, &ct_priv._x, sizeof(ct_priv._x)); \
			LOG_DBG("<ct/" #_x "> read from storage"); \
			return 0; \
		} \
})

#define CT_SETTINGS_HANDLE_SET_ARR(_x) ({\
		if (!strncmp(name,  #_x, name_len)) { \
			rc = read_cb(cb_arg, ct_priv._x, sizeof(ct_priv._x)); \
			LOG_DBG("<ct/" #_x "> read from storage"); \
			return 0; \
		} \
})

/* set the value from flash into the runtime environment */
int ct_settings_handle_set(const char *name, size_t len,
                    settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	int rc;

    LOG_DBG("set:<ct>\n");

    // is there a separator after this name (so is it a sub-tree or not)
	name_len = settings_name_next(name, &next);

	if (!next) {

        CT_SETTINGS_HANDLE_SET(adv_period);
        CT_SETTINGS_HANDLE_SET(scan_period);

        CT_SETTINGS_HANDLE_SET(adv_ival_min);
        CT_SETTINGS_HANDLE_SET(adv_ival_max);

        CT_SETTINGS_HANDLE_SET(scan_ival);
        CT_SETTINGS_HANDLE_SET(scan_window);

        CT_SETTINGS_HANDLE_SET(tek_rolling_interval);
        CT_SETTINGS_HANDLE_SET(tek_rolling_period);

        CT_SETTINGS_HANDLE_SET_ARR(device_name);
	}

	return -ENOENT;
}

#define CT_SETTINGS_HANDLE_COMMIT(_x,_v) ({\
    if (!ct_priv._x) { \
        ct_priv._x = _v; \
    } \
})

#define CT_SETTINGS_HANDLE_COMMIT_ARR(_x,_v) ({\
    if (!ct_priv._x[0]) { \
        memcpy(ct_priv._x, _v, sizeof(ct_priv._x)); \
    } \
})

/* final check when reading back all values from flash to runtime environment */
int ct_settings_handle_commit(void)
{
	LOG_DBG("commit:<ct>\n");

    CT_SETTINGS_HANDLE_COMMIT(adv_period, CT_DEFAULT_BT_ADV_PERIOD);
    CT_SETTINGS_HANDLE_COMMIT(scan_period, CT_DEFAULT_BT_SCAN_PERIOD);

    CT_SETTINGS_HANDLE_COMMIT(adv_ival_min, CT_DEFAULT_BT_ADV_IVAL_MIN);
    CT_SETTINGS_HANDLE_COMMIT(adv_ival_max, CT_DEFAULT_BT_ADV_IVAL_MAX);

    CT_SETTINGS_HANDLE_COMMIT(scan_ival, CT_DEFAULT_BT_SCAN_IVAL);
    CT_SETTINGS_HANDLE_COMMIT(scan_window, CT_DEFAULT_BT_SCAN_WINDOW);

    CT_SETTINGS_HANDLE_COMMIT(tek_rolling_interval, CT_DEFAULT_TEK_IVAL);
    CT_SETTINGS_HANDLE_COMMIT(tek_rolling_period, CT_DEFAULT_TEK_PERIOD);

    CT_SETTINGS_HANDLE_COMMIT_ARR(device_name, CT_DEFAULT_DEVICENAME);

	return 0;
}

#define CT_SETTINGS_HANDLE_EXPORT(_x) ({\
    if(ct_priv._x) { \
        (void)cb("ct/" #_x, &ct_priv._x, sizeof(ct_priv._x)); \
    } \
})

#define CT_SETTINGS_HANDLE_EXPORT_ARR(_x) ({\
    if (ct_priv._x[0]) { \
        (void)cb("ct/" #_x, ct_priv._x, sizeof(ct_priv._x)); \
    } \
})

/* called before all values from the runtime environment are stored to flash,
        as preparation */
int ct_settings_handle_export(int (*cb)(const char *name,
			       const void *value, size_t val_len))
{
	LOG_DBG("export keys under <ct> handler\n");

    CT_SETTINGS_HANDLE_EXPORT(adv_period);
    CT_SETTINGS_HANDLE_EXPORT(scan_period);

    CT_SETTINGS_HANDLE_EXPORT(adv_ival_min);
    CT_SETTINGS_HANDLE_EXPORT(adv_ival_max);

    CT_SETTINGS_HANDLE_EXPORT(scan_ival);
    CT_SETTINGS_HANDLE_EXPORT(scan_window);

    CT_SETTINGS_HANDLE_EXPORT(tek_rolling_interval);
    CT_SETTINGS_HANDLE_EXPORT(tek_rolling_period);

    CT_SETTINGS_HANDLE_EXPORT_ARR(device_name);

	return 0;
}
