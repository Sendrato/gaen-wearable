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

#include <zephyr/types.h>
#include <device.h>
#include <stddef.h>
#include <stdio.h>

#include <sys/util.h>

#include "ui.h"

#include <drivers/gpio.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);
/*
 * Devicetree helper macro which gets the 'flags' cell from a 'gpios'
 * property, or returns 0 if the property has no 'flags' cell.
 */

#define FLAGS_OR_ZERO(node)						\
	COND_CODE_1(DT_PHA_HAS_CELL(node, gpios, flags),		\
		    (DT_GPIO_FLAGS(node, gpios)),			\
		    (0))

/*
 * Get button configuration from the devicetree sw0 alias.
 *
 * At least a GPIO device and pin number must be provided. The 'flags'
 * cell is optional.
 */
#define SW0_NODE	DT_ALIAS(sw0)

#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
#define SW0_GPIO_LABEL	DT_GPIO_LABEL(SW0_NODE, gpios)
#define SW0_GPIO_PIN	DT_GPIO_PIN(SW0_NODE, gpios)
#define SW0_GPIO_FLAGS	(GPIO_INPUT | FLAGS_OR_ZERO(SW0_NODE))
#else
#error "Unsupported board: sw0 devicetree alias is not defined"
#define SW0_GPIO_LABEL	""
#define SW0_GPIO_PIN	0
#define SW0_GPIO_FLAGS	0
#endif

#define LED_NODE(x) DT_ALIAS(led##x)
#define LED_PIN(x) DT_GPIO_PIN(LED_NODE(x), gpios)
#define LED_FLAGS(x) FLAGS_OR_ZERO(LED_NODE(x))


/* button timer */
#define UI_BTN_TIMEOUT               K_MSEC(300)
#define UI_BTN_LONGPRESS_TIMEOUT_CNT 5
static struct k_delayed_work _btn_work;
static struct gpio_callback  _btn_gpio_cb;

const static struct device *_dev_btn;

static int _btn_click_cnt;
static int _btn_longpress_cnt;

static ui_btn_callback_handler_t _btn_cb;


#define UI_LED_TIMEOUT               K_MSEC(500)
#define UI_HAPTIC_TIMEOUT            K_MSEC(250)

static struct k_delayed_work _led_work;

const static struct device *_dev_led;
static uint8_t _led_blink_state[6];

static const gpio_pin_t _led_pins[] = {
#if DT_NODE_EXISTS(LED_NODE(0))
    LED_PIN(0),
#endif
#if DT_NODE_EXISTS(LED_NODE(1))
    LED_PIN(1),
#endif
#if DT_NODE_EXISTS(LED_NODE(2))
    LED_PIN(2),
#endif
#if DT_NODE_EXISTS(LED_NODE(3))
    LED_PIN(3),
#endif
#if DT_NODE_EXISTS(LED_NODE(4))
    LED_PIN(4),
#endif
#if DT_NODE_EXISTS(LED_NODE(5))
    LED_PIN(5),
#endif
};

static const gpio_flags_t _led_flags[] = {
#if DT_NODE_EXISTS(LED_NODE(0))
    LED_FLAGS(0),
#endif
#if DT_NODE_EXISTS(LED_NODE(1))
    LED_FLAGS(1),
#endif
#if DT_NODE_EXISTS(LED_NODE(2))
    LED_FLAGS(2),
#endif
#if DT_NODE_EXISTS(LED_NODE(3))
    LED_FLAGS(3),
#endif
#if DT_NODE_EXISTS(LED_NODE(4))
    LED_FLAGS(4),
#endif
#if DT_NODE_EXISTS(LED_NODE(5))
    LED_FLAGS(5),
#endif
};

const static struct device *_dev_haptic;
static struct k_delayed_work _haptic_work;
static uint8_t _haptic_state;

#define HAPTIC_NODE	DT_INST(0, gpio_haptic)

#if DT_NODE_HAS_STATUS(HAPTIC_NODE, okay)
#define HAPTIC_GPIO_LABEL	DT_GPIO_LABEL(HAPTIC_NODE, gpios)
#define HAPTIC_GPIO_PIN	    DT_GPIO_PIN(HAPTIC_NODE, gpios)
#define HAPTIC_GPIO_FLAGS	(GPIO_INPUT | FLAGS_OR_ZERO(HAPTIC_NODE))
#else
#warning "Haptic devicetree is not defined"
#define HAPTIC_GPIO_LABEL	""
#define HAPTIC_GPIO_PIN	0
#define HAPTIC_GPIO_FLAGS	0
#endif


static void ui_btn_timeout(struct k_work *work)
{
    // the work pointer can be used to handle more than 1 button.
    // ==>  CONTAINER_OF(...)

    int state = gpio_pin_get(_dev_btn, SW0_GPIO_PIN);

    if(state>0) {

        if(_btn_longpress_cnt++ > UI_BTN_LONGPRESS_TIMEOUT_CNT) {
            LOG_INF(" long click");

            if(_btn_cb)
                _btn_cb(0, UI_BTN_LONGPRESS);

        } else {
            k_delayed_work_submit(&_btn_work, UI_BTN_TIMEOUT);
        }

    } else {
        LOG_INF("clk cnt = %i", _btn_click_cnt);

        if(_btn_cb)
            _btn_cb(0, _btn_click_cnt);

        _btn_longpress_cnt = 0;
    }
    _btn_click_cnt = 0;
}

static void ui_btn_state_changed(const struct device *dev,
            struct gpio_callback *cb, unsigned int pins)
{

    int state = gpio_pin_get(_dev_btn, SW0_GPIO_PIN);

    LOG_INF("buttons state changed to %u", state);

    if(state > 0) {
        k_delayed_work_submit(&_btn_work, UI_BTN_TIMEOUT);

    } else {
        if(_btn_longpress_cnt>1) {
            _btn_click_cnt = 0;
        } else {
            _btn_click_cnt++;
        }
        _btn_longpress_cnt = 0;
    }
}

void ui_btn_set_callback(ui_btn_callback_handler_t cb)
{
        _btn_cb = cb;
}

static void ui_led_timeout(struct k_work *work)
{
    int active = 0;

    for(int i= 0; i < ARRAY_SIZE(_led_pins); i++) {

        gpio_pin_set(_dev_led, _led_pins[i], _led_blink_state[i] & 1);

        if(_led_blink_state[i]==0) {
            continue;
        } else if ((_led_blink_state[i] + 1) == UI_BLINK_INFINITE) {
            _led_blink_state[i] = UI_BLINK_INFINITE;
        } else {
            _led_blink_state[i] -= 1;
        }

        active = 1;
    }

    if(active)
        k_delayed_work_submit(&_led_work, UI_LED_TIMEOUT);

}

void ui_led_set(int led, uint8_t val)
{
    if(led >= ARRAY_SIZE(_led_pins))
        return;

    //overrule blinker worker.
    _led_blink_state[led] = 0;
    val = (val == 0) ? 0 : 1;
    gpio_pin_set(_dev_led, _led_pins[led], val);
}

void ui_led_blink(int led, uint8_t cnt)
{
    if(led >= ARRAY_SIZE(_led_blink_state))
        return;

    // on/off is encoded in state.
    _led_blink_state[led] = cnt*2;
    if (cnt >= 100)
        _led_blink_state[led] = UI_BLINK_INFINITE;

    k_delayed_work_submit(&_led_work, K_NO_WAIT);
}


static void ui_haptic_timeout(struct k_work *work)
{
    if (!_dev_haptic)
        return;

    // Update haptic engine,
    gpio_pin_set(_dev_haptic, HAPTIC_GPIO_PIN, _haptic_state & 1);

    // When 0, engine is off and we are done.
    if (_haptic_state==0) {
        return;
    // Reset infinit-blink..
    } else if ((_haptic_state + 1) == UI_BLINK_INFINITE) {
        _haptic_state = UI_BLINK_INFINITE;
    } else {
        _haptic_state -= 1;
    }

    k_delayed_work_submit(&_haptic_work, UI_HAPTIC_TIMEOUT);
}

void ui_haptic_set(uint8_t val)
{
    if (!_dev_haptic)
        return;

    //overrule haptic worker.
    k_delayed_work_cancel(&_haptic_work);
    val = (val == 0) ? 0 : 1;
    gpio_pin_set(_dev_haptic, HAPTIC_GPIO_PIN, val);
}

void ui_haptic_blink(uint8_t cnt)
{
    if (!_dev_haptic)
        return;

    // on/off is encoded in state.
    _haptic_state = cnt*2;
    if (cnt >= 100)
        _haptic_state = UI_BLINK_INFINITE;

    k_delayed_work_submit(&_haptic_work, K_NO_WAIT);
}





void ui_init(void)
{
	int ret;

	k_delayed_work_init(&_btn_work, ui_btn_timeout);
    k_delayed_work_init(&_led_work, ui_led_timeout);
    k_delayed_work_init(&_haptic_work, ui_haptic_timeout);

	_dev_btn = device_get_binding(SW0_GPIO_LABEL);
	if (_dev_btn == NULL) {
        LOG_ERR("Error: didn't find %s device", SW0_GPIO_LABEL);
		return;
	}

	ret = gpio_pin_configure(_dev_btn, SW0_GPIO_PIN, SW0_GPIO_FLAGS);
	if (ret != 0) {
        LOG_ERR("Error %d: failed to configure %s pin %d",
		       ret, SW0_GPIO_LABEL, SW0_GPIO_PIN);
		return;
	}

	ret = gpio_pin_interrupt_configure(_dev_btn, SW0_GPIO_PIN,
                    GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
        LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
			ret, SW0_GPIO_LABEL, SW0_GPIO_PIN);
		return;
	}

	gpio_init_callback(&_btn_gpio_cb, ui_btn_state_changed, BIT(SW0_GPIO_PIN));
	gpio_add_callback(_dev_btn, &_btn_gpio_cb);
    LOG_INF("Set up button at %s pin %d", SW0_GPIO_LABEL, SW0_GPIO_PIN);

	_dev_led = device_get_binding("GPIO_0");
	if (_dev_led == NULL) {
		LOG_ERR("Error: didn't find 'GPIO_0' device");
		return;
	}

    for(int i= 0; i < ARRAY_SIZE(_led_pins); i++) {
        ret = gpio_pin_configure(_dev_led, _led_pins[i],
                    _led_flags[i] | GPIO_OUTPUT);
        if (ret != 0) {
            LOG_ERR("Warning %d: failed to configure pin 'led%d'", ret, i);
        } else {
            ui_led_off(i);
            _led_blink_state[i] = 0;
        }
    }

    _dev_haptic = device_get_binding(HAPTIC_GPIO_LABEL);
	if (_dev_haptic == NULL) {
        LOG_ERR("Warning: didn't find haptic (%s) device", HAPTIC_GPIO_LABEL);
	} else {
        ret = gpio_pin_configure(_dev_haptic, HAPTIC_GPIO_PIN, GPIO_OUTPUT);
        if (ret != 0) {
            LOG_ERR("Error %d: failed to configure haptic %d",
                        ret, HAPTIC_GPIO_PIN);
        }
        ui_haptic_off();
    }

    _btn_click_cnt = 0;
    _btn_longpress_cnt = 0;
}
