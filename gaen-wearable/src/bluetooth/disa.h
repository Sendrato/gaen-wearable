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
 *  @brief GATT Authenticated Device Information Service
 */

#ifndef __GATT_DISA_H
#define __GATT_DISA_H

/**
 * @brief Initialise the DISA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_disa_init(void);

/**
 * @brief Start the DISA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_disa_start(void);

/**
 * @brief Stop the DISA-service.
 * @return 0 on success, negative errno code on failure.
 */
int bt_gatt_disa_stop(void);

#endif /* __GATT_DISA_H */
