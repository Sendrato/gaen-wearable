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

#include <tinycrypt/hkdf.h>
#include <tinycrypt/hmac.h>
#include <tinycrypt/hmac_prng.h>
#include <tinycrypt/sha256.h>
#include <tinycrypt/aes.h>
#include <tinycrypt/ctr_mode.h>
#include <tinycrypt/constants.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <posix/time.h>
#include <posix/sys/time.h>

#include <zephyr.h>
#include <device.h>
#include <drivers/entropy.h>

#include <settings/settings.h>
#include <drivers/hwinfo.h>

#include "ct.h"
#include "ct_crypto.h"
#include "ct_settings.h"

static uint8_t psk_rpik[] = "EN-RPIK";
static uint8_t psk_rpi[]  = "EN-RPI";
static uint8_t psk_aemk[] = "EN-AEMK";


// ENIntervalNumber(..)
static uint32_t ct_crypto_intervalNumber(uint64_t time) {
    return time / (ct_priv.tek_rolling_interval);
}



uint32_t ct_crypto_intervalNumber_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ct_crypto_intervalNumber(ts.tv_sec);
}



// When setting up the device for exposure detection, the first
//   Temporary Exposure Key is generated on the device and associated with an
//   ENIntervalNumber i, corresponding to the time from which the key is valid.
//   That value is aligned with the TEKRollingPeriod and is derived as follows:
uint32_t ct_crypto_intervalNumberAligned_now(void)
{
    return (ct_crypto_intervalNumber_now() / ct_priv.tek_rolling_period )
                * ct_priv.tek_rolling_period;
}



// The CRNG function designates a cryptographic random number generator:
// Output <-- CRNG(OutputLength)
// --
// NOTE: this function is adjusted to generate a random TEK of size TEK_SIZE.
static int ct_crypto_crng(uint8_t *tek)
{
    const struct device *dev;
    uint8_t hwid[12];
    uint8_t seed[32] = {0};
    uint32_t add_random[8];
    struct tc_hmac_prng_struct h;

    hwinfo_get_device_id((uint8_t*)hwid, sizeof(hwid));

	dev = device_get_binding(DT_CHOSEN_ZEPHYR_ENTROPY_LABEL);
	if (!dev) {
		printk("error: no random device\n");
	} else {
        entropy_get_entropy(dev,seed, sizeof(seed));
    }

    memset(&h, 0x0, sizeof(h));

    for(int i=0; i<ARRAY_SIZE(add_random);i++) {
        add_random[i] = k_cycle_get_32();
    }

    for(int i=1; i<ARRAY_SIZE(add_random);i++) {
        add_random[i] = add_random[i] * add_random[i-1];
    }

	(void)tc_hmac_prng_init(&h, hwid, sizeof(hwid));
	(void)tc_hmac_prng_reseed(&h, seed, sizeof(seed), (uint8_t*)add_random,
                        sizeof(add_random));
    (void)tc_hmac_prng_generate(tek, TEK_SIZE, &h);

    return 0;
}



// in-place generation of a new tek
int ct_crypto_calc_tek( uint8_t *tek )
{
    if(!tek)
        return -EINVAL;

    // A new tek is simply a cryptographic random number of size TEK_SIZE
    ct_crypto_crng(tek);

    return 0;
}



// HKDF designates the HKDF function as defined by IETF RFC 5869,
// using the SHA-256 hash function:
// Output <== HKDF(Key, Salt, Info, OutputLength)
// RPIK_i <== HKDF(tek-i, NULL, UTF8("EN-RPIK"), 16)
int ct_crypto_calc_rpik(uint8_t *tek, uint8_t *rpik)
{
    (void) hkdf_sha256(rpik, AEMK_SIZE,
                        tek, TEK_SIZE,
                        NULL, 0,
                        psk_rpik, strlen(psk_rpik));

    return 0;
}



// Rolling Proximity Identifiers are privacy-preserving identifiers that are
//  broadcast in Bluetooth payloads. Each time the Bluetooth Low Energy MAC
//  randomized address changes, we derive a new Rolling Proximity Identifier
//  using the Rolling Proximity Identifier Key:
// RPI_{i,j} <== AES128(RPIK_i,PaddedData_j)
// Where:
// - j is the Unix Epoch Time at the moment the roll occurs
// - ENIN_j <== ENIntervalNumber( j )
// - PaddedData is the following sequence of 16 bytes:
// - PaddedDataj[0...5] = UTF8("EN-RPI")
// - PaddedDataj[6...11] = 0x000000000000
// - PaddedDataj[12...15] = ENIN_j
int ct_crypto_calc_rpi(uint32_t enin_j, uint8_t *rpik, uint8_t *rpi)
{
    uint8_t padding[16];
   	struct tc_aes_key_sched_struct s;

    for(int i=0;i<6;i++)
        padding[i] = psk_rpi[i];

    for(int i=6;i<12;i++)
        padding[i] = 0;

#if 1
    //Little Endianness
    *((uint32_t*)&padding[12]) = enin_j;
#else
    //Big Endianness
    padding[15] = (enin_j & 0x000000FF) >>  0;
    padding[14] = (enin_j & 0x0000FF00) >>  8;
    padding[13] = (enin_j & 0x00FF0000) >> 16;
    padding[12] = (enin_j & 0xFF000000) >> 24;
#endif

	(void)tc_aes128_set_encrypt_key(&s, rpik);
	(void)tc_aes_encrypt(rpi, padding, &s);

    return 0;
}



int ct_crypto_calc_aemk(uint8_t *tek, uint8_t *aemk)
{
    (void) hkdf_sha256(aemk, AEMK_SIZE,
                        tek, TEK_SIZE,
                        NULL, 0,
                        psk_aemk, strlen(psk_aemk));
    return 0;
}



int ct_crypto_calc_aem(uint8_t *aemk, uint8_t *rpi, uint8_t *metadata,
                    uint8_t *aem)
{
    struct tc_aes_key_sched_struct sched;

    (void) tc_aes128_set_encrypt_key(&sched, aemk);
    (void) tc_ctr_mode(aem, AEM_SIZE, metadata, META_SIZE, rpi, &sched);

    return 0;
}



int ct_crypto_init(void)
{
    return 0;
}
