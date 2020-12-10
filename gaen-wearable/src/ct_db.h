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
 * @brief Data storage of TEKs and RPIs.
 */

#ifndef __CT_DB_H
#define __CT_DB_H

#include <string.h>
#include <zephyr/types.h>

#include <sys/slist.h>

/**
 * @brief Initialise database.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_init(void);

/**
 * @brief Perform a database-tick, allowing database to do data-management.
 *
 * Especially when using an external flash-chip, this tick is important as it
 * ensures that local buffers will be verified and pushed into the NVM.
 *
 * @param [in]  ival  : rolling-interval at which the tick is performed.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_tick(uint32_t ival);

/**
 * @brief Clear local buffers and (external) flash storage.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_clear(void);

/**
 * @brief Clear local TEK buffer.
 * @return 0 on success, negative errno code on failure.
 */
int ct_db_tek_clear(void);

/**
 * @brief Clear local RPI buffer.
 * @return 0 on success, negative errno code on failure.
 */
int ct_db_rpi_clear(void);

/**
 * @brief Add a new TEK to the database.
 *
 * @param [in]  tek   : pointer to a TEK_SIZE-byte array containing TEK.
 * @param [in]  ival  : rolling-interval at which the TEK starts.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_tek_add(uint8_t *tek, uint32_t ival);

/**
 * @brief Retrieve the number of stored TEKs.
 *
 * The return value is the sum of all TEKs stored in the local buffer and
 * stored in the external flash.
 *
 * @param [out]  cnt   : number of TEKs.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_tek_get_cnt(uint16_t *cnt);

/**
 * @brief Retrieve the n'th TEK from memory
 *
 * When n=0 the oldest TEK is retrieved. When n=n the newest TEK is retrieved.
 * If the n'th TEK does not exists, the values of `tek` and `ival` are undefined.
 *
 * @param [in]  n     : n'th value in which we are interested.
 * @param [out] tek   : pointer to a TEK_SIZE-byte array in which the TEK will be stored
 * @param [out] ival  : rolling-interval at which the TEK starts.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_tek_get(uint16_t n, uint8_t *tek, uint32_t *ival);

/**
 * @brief Retrieve the last TEK from memory
 *
 * @param [out] tek   : pointer to a TEK_SIZE-byte array in which the TEK will be stored
 * @param [out] ival  : rolling-interval at which the TEK starts.
 * @return 0 on success, negative errno code on [flash] failure.
 * @return -EINVAL when database is empty.
 */
int ct_db_tek_get_last(uint8_t *tek, uint32_t *ival);

/**
 * @brief Add a new RPI to the database.
 *
 * @param [in]  rpi   : pointer to a RPI_SIZE-byte array containing the RPI.
 * @param [in]  aem   : pointer to a AEM_SIZE-byte array containing the AEM.
 * @param [in]  rssi  : RSSI value at which the RPI is received [dB]
 * @param [in]  ival  : rolling-interval at which the RPI is received.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_rpi_add(uint8_t *rpi, uint8_t *aem, int8_t rssi, uint32_t ival);

/**
 * @brief Retrieve the number of stored RPIs.
 *
 * The return value is the sum of all RPIs stored in the local buffer and
 * stored in the external flash.
 *
 * @param [out]  cnt   : number of RPIs.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_rpi_get_cnt(uint16_t *cnt);

/**
 * @brief Retrieve the n'th RPI from memory
 *
 * When n=0 the oldest RPI is retrieved. When n=n the newest RPI is retrieved.
 * If the n'th RPI does not exists, the out-params are undefined.
 *
 * @param [in]  n     : n'th value in which we are interested.
 * @param [out] rpi   : pointer to a RPI_SIZE-byte array in which the RPI will be stored
 * @param [out] aem   : pointer to a AEM_SIZE-byte array in which the AEM will be stored
 * @param [out] rssi  : RSSI value at which the RPI is received [dB].
 * @param [out] cnt   : number of observations.
 * @param [out] ival  : highest rolling-interval at which the RPI is observed.
 * @return 0 on success, negative errno code on [flash] failure.
 */
int ct_db_rpi_get(uint16_t n, uint8_t *rpi, uint8_t *aem, int8_t *rssi,
                uint8_t *cnt, uint32_t *ival_last);

#endif /* __CT_DB_H */
