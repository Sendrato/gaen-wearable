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
 * @brief Contact Tracing / GAEN configuration application.
 *
 * When this app is active a BLE Central Device is able to connect to the
 * wearable to retrieve stored TEK/RPI data and to update protocol settings.
 */

#ifndef __CT_APP_ENC_H
#define __CT_APP_ENC_H

#include <string.h>
#include <zephyr/types.h>

/**
 * @brief Initialise the ENC-application.
 * @return 0 on success, negative errno code on failure.
 */
int ct_app_enc_init(void);

/**
 * @brief Start the ENC-application.
 *
 * This is a none-blocking call. The application will start / allocate resources
 * after a small amount of time to allow other apps to gracefully terminate.
 *
 * @return 0 on success, negative errno code on failure.
 */
int ct_app_enc_start(void);

/**
 * @brief Stop the ENC-application.
 *
 * This is a none-blocking call. After this call the application will
 * gracefully terminate any open connections and clean up system allocations.
 *
 * @return 0 on success, negative errno code on failure.
 */
int ct_app_enc_stop(void);

#endif /* __CT_APP_ENC_H */
