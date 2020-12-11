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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <device.h>
#include <zephyr.h>
#include <init.h>
#include <drivers/gpio.h>
#include <drivers/adc.h>
#include <drivers/sensor.h>
#include <logging/log.h>

#ifdef CONFIG_ADC_NRFX_SAADC
#include <hal/nrf_saadc.h>
#endif


#include "battery.h"

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#define BATT_NODE DT_NODELABEL(adc)

#if DT_NODE_HAS_STATUS(BATT_NODE, okay)
#define BATT_NODE_LABEL         DT_LABEL(BATT_NODE)
#else
#define BATT_NODE_LABEL         "NONE"

#endif

/** A discharge curve calibrated from LiPo batteries.
 */
const struct battery_level_point lipo[] = {

    /* "Curve" here eyeballed from captured data for a full load
     * that started with a charge of 3.96 V and dropped about
     * linearly to 3.58 V over 15 hours.  It then dropped rapidly
     * to 3.10 V over one hour, at which point it stopped
     * transmitting.
     *
     * Based on eyeball comparisons we'll say that 15/16 of life
     * goes between 3.95 and 3.55 V, and 1/16 goes between 3.55 V
     * and 3.1 V.
     */

    { 10000, 3950 },
    { 625, 3550 },
    { 0, 3100 },
};


/** A discharge curve calibrated from cr2032 batteries.
 */
const struct battery_level_point cr2032[] = {

    /* to be done, user enegizer datasheet as estimate ... */

    { 10000, 2950 },
    { 4500, 2900 },
    { 1500, 2550 },
    { 0, 1800 },
};




const static struct device *_adc_dev;
static struct adc_channel_cfg _adc_cfg;
static struct adc_sequence _adc_seq;

static int16_t _adc_raw;

int battery_init(void)
{
    int rc;

    _adc_dev = device_get_binding(BATT_NODE_LABEL);
    if (_adc_dev == NULL) {
        LOG_ERR("Failed to get ADC:  " BATT_NODE_LABEL);
        return -ENOENT;
    }


    _adc_seq = (struct adc_sequence){
        .channels = BIT(0),
        .buffer = &_adc_raw,
        .buffer_size = sizeof(_adc_raw),
        .oversampling = 4,
        .calibrate = true,
    };

#ifdef CONFIG_ADC_NRFX_SAADC
    _adc_cfg = (struct adc_channel_cfg){
        .gain = ADC_GAIN_1_6,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
        .input_positive = NRF_SAADC_INPUT_VDD,
    };

    _adc_seq.resolution = 14;
#else /* CONFIG_ADC_var */
#warning Unsupported ADC
    _adc_dev = NULL;
#endif /* CONFIG_ADC_var */

    if (_adc_dev == NULL) {
        LOG_ERR("Unsupported ADC");
        return -ENOENT;
    }

    rc = adc_channel_setup(_adc_dev, &_adc_cfg);
    if(rc)
        LOG_ERR("Setup AIN_VDD got %d", rc);

    battery_sample();

    return rc;
}

int battery_sample(void)
{
    int rc;

    if (_adc_dev == NULL) {
        return -ENOENT;
    }

    rc = adc_read(_adc_dev, &_adc_seq);
    _adc_seq.calibrate = false;
    if (rc == 0) {
        int32_t val = _adc_raw;

        adc_raw_to_millivolts(adc_ref_internal(_adc_dev),
                        _adc_cfg.gain,
                        _adc_seq.resolution,
                        &val);
        rc = val;
        LOG_INF("raw batt %d mV",  val);

        /* resistor correction to be added for externally measured voltages */
    }

    return rc;
}

/* battery level in pptt (parts per 10.000) */
unsigned int battery_level_pptt(unsigned int batt_mV,
                const struct battery_level_point *curve)
{
    const struct battery_level_point *pb = curve;

    if (batt_mV >= pb->lvl_mV) {
        /* Measured voltage above highest point, cap at maximum. */
        return pb->lvl_pptt;
    }
    /* Go down to the last point at or below the measured voltage. */
    while ((pb->lvl_pptt > 0)
           && (batt_mV < pb->lvl_mV)) {
        ++pb;
    }
    if (batt_mV < pb->lvl_mV) {
        /* Below lowest point, cap at minimum */
        return pb->lvl_pptt;
    }

    /* Linear interpolation between below and above points. */
    const struct battery_level_point *pa = pb - 1;

    return pb->lvl_pptt
           + ((pa->lvl_pptt - pb->lvl_pptt)
          * (batt_mV - pb->lvl_mV)
          / (pa->lvl_mV - pb->lvl_mV));
}
