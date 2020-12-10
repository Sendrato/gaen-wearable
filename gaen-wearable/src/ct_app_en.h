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
 * @brief Contact Tracing / GAEN application.
 *
 * This file holds the Contact Tracing / Google-Apple Exposure Notication
 * application. It manages the required crypto, Bluetooth stack, address
 * rotations, TEK and RPI updates.
 */

#ifndef __CT_APP_EN_H
#define __CT_APP_EN_H

#include <string.h>
#include <zephyr/types.h>

/**
 * @brief Initialise the EN-application.
 * @return 0 on success, negative errno code on failure.
 */
int ct_app_en_init(void);

/**
 * @brief Start the EN-application.
 *
 * This is a none-blocking call. The application will start / allocate resources
 * after a small amount of time to allow other apps to gracefully terminate.
 *
 * @return 0 on success, negative errno code on failure.
 */
int ct_app_en_start(void);

/**
 * @brief Stop the EN-application.
 *
 * This is a none-blocking call. After this call the application will
 * gracefully terminate any open connections and clean up system allocations.
 *
 * @return 0 on success, negative errno code on failure.
 */
int ct_app_en_stop(void);

#endif /* __CT_APP_EN_H */
