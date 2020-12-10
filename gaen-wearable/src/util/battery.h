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
 * Copyright (c) 2018-2019 Peter Bigot Consulting, LLC
 *
 * Cleanup and extension of original files regarding ADC initalisation,
 * sampling and correction.
 */

/**
 * @file
 * @brief battery-helpers.
 */

#ifndef __BATTERY_H_
#define __BATTERY_H_

/**
 * @brief A point in a battery discharge curve sequence.
 *
 * A discharge curve is defined as a sequence of these points, where
 * the first point has #lvl_pptt set to 10000 and the last point has
 * #lvl_pptt set to zero.  Both #lvl_pptt and #lvl_mV should be
 * monotonic decreasing within the sequence.
 */
struct battery_level_point {
	/** Remaining life at #lvl_mV. */
	uint16_t lvl_pptt;
	/** Battery voltage at #lvl_pptt remaining life. */
	uint16_t lvl_mV;
};

/**
 * @brief lipo curve
 */
extern const struct battery_level_point lipo[];

/**
 * @brief cr2032 curve
 */
extern const struct battery_level_point cr2032[];

/**
 * @brief Initialise the battery-subsystem.
 * @return 0 on success, negative errno code on failure.
 */
int battery_init(void);

/**
 * @brief Sample the remaining battery voltage.
 * @return milliVolt readout.
 */
int battery_sample(void);

/**
 * @brief Compute battery level-point.
 * @param [in] batt_mv : milliVolt readout.
 * @param [in] curve   : pointer to curve-structure of used battery-type.
 * @return pptt of milliVolt sample.
 */
unsigned int battery_level_pptt(unsigned int batt_mV,
				const struct battery_level_point *curve);

#endif /* __BATTERY_H_ */
