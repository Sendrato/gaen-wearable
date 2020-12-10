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
 * @brief UI drivers: Leds, buttons, haptic.
 */

#ifndef __UI_H
#define __UI_H

#include <string.h>

#include <zephyr/types.h>
#include <drivers/gpio.h>

#define UI_BTN_LONGPRESS 0x80
#define UI_BTN_CLK_MAX   0x7F

#define UI_BLINK_INFINITE   0xEF

// Note: board dependent!
#define UI_LED_GREEN 0
#define UI_LED_RED   1
#define UI_LED_BLUE  2

/**
 * @brief Initialise the EN-application.
 * @return 0 on success, negative errno code on failure.
 */
void ui_init(void);

/**
 * @typedef callback for btn interrupt
 * @param btn : button-id
 * @param clk : number of button-presses.
 */
typedef void (*ui_btn_callback_handler_t)(int btn, uint8_t clk);

/**
* @brief Set button callback
* @param [in] cb : user callback
*/
void ui_btn_set_callback(ui_btn_callback_handler_t cb);

/**
* @brief Turn a led on or off.
* @param [in] led : led-id
* @param [in] val : on (1) or off (0).
*/
void ui_led_set(int led, uint8_t val);

#define ui_led_on( _l) ui_led_set(_l, 1)
#define ui_led_off(_l) ui_led_set(_l, 0)

/**
* @brief Blink a led.
* @param [in] led : led-id
* @param [in] cnt : number of blinks. Use @ref UI_BLINK_INFINITE for infinite.
*/
void ui_led_blink(int led, uint8_t cnt);

/**
* @brief Turn the haptic engine on or off.
* @param [in] val : on (1) or off (0).
*/
void ui_haptic_set(uint8_t val);

#define ui_haptic_on() ui_haptic_set(1)
#define ui_haptic_off() ui_haptic_set(0)

/**
* @brief Blink the haptic engine.
* @param [in] cnt : number of blinks. Use @ref UI_BLINK_INFINITE for infinite.
*/
void ui_haptic_blink(uint8_t cnt);


#endif /* __UI_H */
