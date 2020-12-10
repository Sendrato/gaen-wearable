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

/**
 * @file
 * @brief Contact Tracing (GAEN) wide definitions and default settings.
 */

#ifndef __CT_H
#define __CT_H

#include <string.h>
#include <zephyr/types.h>

#define CT_INFO_MODEL "CT WEARABLE"
#define CT_INFO_MANUF "SynchronicIT/Sendrato"
#define CT_INFO_FWREV "v1.0.0"

/**
 * @def CT_BATT_TYPE
 * @brief Type of battery which is used.
 *
 * Battery status is calculacted based on the type of used battery.
 * See util/battery.h for options.
 */
#define CT_BATT_TYPE lipo

/**
 * @def CT_DEFAULT_DEVICENAME
 * @brief Device name.
 *
 * The device name will be published with bluetooth advertisments when the
 * wearable is put in config-mode. The device name should be 10 characters long.
 */
#define CT_DEFAULT_DEVICENAME "CTWEARABLE"

/**
 * @def CT_DEFAULT_BT_ADV_PERIOD
 * @brief Duration of GAEN advertisement period in milliseconds.
 *
 * The GAEN stack alternates between advertisement and scanning. This define
 * specifies the duration [in milliseconds] of the advertisement period.
 */
#define CT_DEFAULT_BT_ADV_PERIOD  4500

/**
 * @def CT_DEFAULT_BT_SCAN_PERIOD
 * @brief Duration of GAEN scanning period in milliseconds.
 *
 * The GAEN stack alternates between advertisement and scanning. This define
 * specifies the duration [in milliseconds] of the scanning period.
 */
#define CT_DEFAULT_BT_SCAN_PERIOD  500

/**
 * @def CT_DEFAULT_BT_ADV_IVAL_MIN
 * @brief Minimum interval in between consecutive GAEN advertisements.
 *
 * Specified according to the Bluetooth Specification in steps of 0.625ms.
 * Default value: 320 * 0.625 = 200 [ms]
 */
#define CT_DEFAULT_BT_ADV_IVAL_MIN  320

/**
 * @def CT_DEFAULT_BT_ADV_IVAL_MAX
 * @brief Maximum interval in between consecutive GAEN advertisements.
 *
 * Specified according to the Bluetooth Specification in steps of 0.625ms.
 * Default value: 432 * 0.625 = 270 [ms]
 */
#define CT_DEFAULT_BT_ADV_IVAL_MAX  432

/**
 * @def CT_DEFAULT_BT_SCAN_IVAL
 * @brief Scan interval during the GAEN Scanning period.
 *
 * Specified according to the Bluetooth Specification in steps of 0.625ms.
 * Default value: 96 * 0.625 = 60 [ms]
 */
#define CT_DEFAULT_BT_SCAN_IVAL    96

/**
 * @def CT_DEFAULT_BT_SCAN_WINDOW
 * @brief Scan window during the GAEN Scanning period.
 *
 * Specified according to the Bluetooth Specification in steps of 0.625ms.
 * Default value: 48 * 0.625 = 30 [ms]
 */
#define CT_DEFAULT_BT_SCAN_WINDOW  48

/**
 * @def CT_DEFAULT_TEK_IVAL
 * @brief TEK Rolling Interval
 */
#define CT_DEFAULT_TEK_IVAL    600

/**
 * @def CT_DEFAULT_TEK_IVAL
 * @brief TEK Rolling Period
 */
#define CT_DEFAULT_TEK_PERIOD  144

// GAEN data-size definitions
#define TEK_SIZE      16
#define RPIK_SIZE     16
#define RPI_SIZE      16
#define AEMK_SIZE     16
#define AEM_SIZE       4
#define META_SIZE      4

/**
 * @typedef ct_app_id_t
 * @brief Contact Tracing Applications which can be acivated.
 */
typedef enum {
    CT_APP_MAIN = 0,    /**< Main application. Triggered after bootup */
    CT_APP_EN,          /**< GAEN application */
    CT_APP_ENC,         /**< GAEN config application. Used to offload data */
} ct_app_id_t;

/**
 * @typedef ct_event_t
 * @brief Application events.
 */
typedef enum {
    /* Generic events */
    CT_EVENT_NONE = 0,
    CT_EVENT_START,
    CT_EVENT_STOP,
    CT_EVENT_BATTERY_EMPTY,

    /* EN events */
    CT_EVENT_NEW_TEK = 64,
    CT_EVENT_NEW_RPI,
    CT_EVENT_START_ADV,
    CT_EVENT_START_SCAN,

    /* ENC events */
    CT_EVENT_CONNECTED = 128,
    CT_EVENT_DISCONNECTED,

    /* Error events */
    CT_EVENT_ERROR = -1,
    CT_EVENT_INVALID_CLOCK,
    CT_EVENT_ENOMEM,
} ct_event_t;

/**
* @brief Callback for applications to notify events.
* @param [in] app   : application which triggers the call @ref ct_app_id_t
* @param [in] event : @ref ct_event_t
*/
void ct_app_event(ct_app_id_t app, ct_event_t event);

#endif /* __CT_H */
