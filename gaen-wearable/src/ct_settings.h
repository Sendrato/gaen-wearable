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
 * @brief Handlers for system settings stored in NVM.
 */

#ifndef __CT_SETTINGS_H
#define __CT_SETTINGS_H

#include <string.h>
#include <zephyr/types.h>

struct ct_settings {
    uint32_t adv_period;
    uint32_t scan_period;

    // BLE advertisement settings in steps of 0.625 [ms].
    uint16_t adv_ival_min;
    uint16_t adv_ival_max;

    // BLE scanning settings in steps of 0.625 [ms].
    uint16_t scan_ival;
    uint16_t scan_window;

    // Discretisation steps in second in which time is sliced.
    // Default is 10 mintues (600sec)
    uint32_t tek_rolling_interval;

    // The rolling-period is the duration for which a Temporary Exposure Key
    //  is valid (in multiples of TEK_ROLLING_INTERVAL). In the default config
    //  rolling-period is defined as 144, achieving a key validity of 24 hours.
    uint32_t tek_rolling_period;

    // Name of device
    unsigned char device_name[10];
};

extern struct ct_settings ct_priv;

#endif /* __CT_SETTINGS_H */
