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

 /** @file
  *  @brief GATT Authenticated Current Time Service
  */

#ifndef __GATT_CTSA_H
#define __GATT_CTSA_H

#include <posix/time.h>
#include <posix/sys/time.h>

/**
 * @brief Notify a connected BLE Central with a clock-update
 */
void bt_gatt_ctsa_notify(void);

/**
 * @brief Format a (Bluetooth) Current-Time buffer into a timespec format.
 * @param [in]  buf : a 10-byte buffer representing the current time.
 * @param [out] ts  : pointer to a timespec structure in which the result is stored.
 */
void bt_gatt_ctsa_buf2timespec(uint8_t *buf, struct timespec* ts);

/**
 * @brief Copy internal clock into a (Bluetooth) Current-Time buffer .
 * @param [out] buf : a 10-byte buffer in which the current time is stored.
 */
void bt_gatt_ctsa_now2buf(uint8_t *buf);

/**
 * @brief Initialise the CTSA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_ctsa_init(void);

/**
 * @brief Start the CTSA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_ctsa_start(void);

/**
 * @brief Stop the CTSA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_ctsa_stop(void);

#endif /* __GATT_CTSA_H */
