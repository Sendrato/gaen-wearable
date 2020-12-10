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

/*
 * Adaption from:
 * Copyright (c) 2018 Nordic Semiconductor ASA
 * Copyright (c) 2016 Intel Corporation
 *
 * Cleanup and extension of original files with the ability to start/stop
 *   the specified gatt-servies and force authenticated access.
 */

/** @file
 *  @brief GATT Authenticated Battery Service
 */

#ifndef __GATT_BASA_H_
#define __GATT_BASA_H_

#include <zephyr/types.h>

/**
 * @brief Read battery level value.
 *
 * Read the characteristic value of the battery level
 *
 * Note that this readout does not perform a battery-sampling.
 * It solely returns the value stored in this characteristic, which is set
 * by @ref bt_gatt_basa_set_battery_level.
 *
 * @return The battery level in percent.
 */
uint8_t bt_gatt_basa_get_battery_level(void);

/**
 * @brief Update battery level value.
 *
 * Update the characteristic value of the battery level
 * This will send a GATT notification to all current subscribers.
 *
 * @param level The battery level in percent.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_basa_set_battery_level(uint8_t level);

/**
 * @brief Initialise the BASA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_basa_init(void);

/**
 * @brief Start the BASA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_basa_start(void);

/**
 * @brief Stop the BASA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_basa_stop(void);

#endif /* __GATT_BASA_H_ */
