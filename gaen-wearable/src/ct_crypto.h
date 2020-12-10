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
 * @brief GAEN Crypto functions.
 */

#ifndef __CT_CRYPTO_H
#define __CT_CRYPTO_H

#include <string.h>
#include <zephyr/types.h>

/**
 * @brief Initialise crypto-system.
 * @return 0 on success, negative errno code on failure.
 */
int ct_crypto_init(void);

/**
 * @brief Compute a rolling interval number based on the current time.
 *
 * This function provides a number for each X minute time window that’s shared
 * between all devices participating in the protocol. These time windows are
 * derived from timestamps in Unix Epoch Time.
 *
 * The X minute time window is retrieved from the systems settings.
 *
 * @return Rolling interval number, a 32-bit, unsigned little-endian value.
 */
uint32_t ct_crypto_intervalNumber_now(void);

/**
 * @brief Compute an aligned rolling interval number based on the current time.
 *
 * When setting up the device for exposure detection, the first
 * Temporary Exposure Key is generated on the device and associated with an
 * ENIntervalNumber i, corresponding to the time from which the key is valid.
 * That value is aligned with the TEKRollingPeriod.
 *
 * @return Rolling interval number, a 32-bit, unsigned little-endian value.
 */
uint32_t ct_crypto_intervalNumberAligned_now(void);

/**
 * @brief Generate a new TEK
 *
 * @param [out] tek : pointer to a TEK_SIZE-byte array in which the TEK will be stored
 * @return 0 on success, negative errno code on failure.
 */
int ct_crypto_calc_tek( uint8_t *tek );

/**
* @brief Generate a new RPI-Key
*
* @param [in]  tek  : pointer to a TEK_SIZE-byte array containing currently active TEK.
* @param [out] rpik : pointer to a RPIK_SIZE-byte array in which the RPI-Key will be stored
* @return 0 on success, negative errno code on failure.
*/
int ct_crypto_calc_rpik(uint8_t *tek, uint8_t *rpik);

/**
* @brief Generate a new RPI
*
* @param [in]  ival : rolling-interval at which the RPI starts.
* @param [in]  rpik : pointer to a RPIK_SIZE-byte array containing the RPI-Key.
* @param [out] rpi  : pointer to a RPI_SIZE-byte array in which the RPI will be stored
* @return 0 on success, negative errno code on failure.
*/
int ct_crypto_calc_rpi(uint32_t ival, uint8_t *rpik, uint8_t *rpi);

/**
* @brief Generate a new AEM-Key
*
* @param [in]  tek  : pointer to a TEK_SIZE-byte array containing currently active TEK.
* @param [out] rpik : pointer to a AEMK_SIZE-byte array in which the AEM-Key will be stored
* @return 0 on success, negative errno code on failure.
*/
int ct_crypto_calc_aemk(uint8_t *tek, uint8_t *aemk);

/**
* @brief Generate a new AEM
*
* @param [in]  aemk : pointer to a AEMK_SIZE-byte array containing the AEM-Key.
* @param [in]  rpi  : pointer to a RPI_SIZE-byte array containing the RPI.
* @param [in]  meta : pointer to a META_SIZE-byte array containing the meta-data.
* @param [out] aem  : pointer to a AEM_SIZE-byte array in which the AEM will be stored
* @return 0 on success, negative errno code on failure.
*/
int ct_crypto_calc_aem(uint8_t *aemk, uint8_t *rpi, uint8_t *meta, uint8_t *aem);

#endif /* __CT_CRYPTO_H */
