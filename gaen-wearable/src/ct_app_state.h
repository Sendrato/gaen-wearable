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
 * @brief Helpers to manage app-state and their worker-queue.
 *
 * Helpers which are postfixed with "_WQ" should only be used in the
 * worker-queue task itself. These helpers depend on data provided by the task.
 */

#ifndef __CT_APP_STATES_H
#define __CT_APP_STATES_H

/**
 * @typedef app_state_t
 * @brief Application states
 *
 * App state definition which is used within app's to determine if app has been
 * activated or stopped by the main-function so async calls can be stop and
 * release their data gracefully.
 */
typedef enum {
    APP_STATE_UNDEF = 0,
    APP_STATE_ACTIVE,
    APP_STATE_STOPPED
} app_state_t;

/**
 * @def APP_STATE_NEXT(_w, _s, _t)
 * @brief Schedule next state _s in time _t on worker-queue _w
 */
#define APP_STATE_NEXT(_w, _s, _t) ({ \
    /* ensure previous functions have been cleared from queue */ \
    k_delayed_work_cancel(_w); \
    /* schedule new function */ \
    k_delayed_work_init(_w, _s); \
    k_delayed_work_submit(_w, _t); \
})

/**
 * @def APP_STATE_NOW(_w, _s)
 * @brief Schedule next state _s now on worker-queue _w
 */
#define APP_STATE_NOW(_w, _s) ({ \
    APP_STATE_NEXT(_w, _s, K_NO_WAIT); \
})

/**
 * @def APP_STATE_CLEAR(_w)
 * @brief Cancel any pending states on worker-queue _w
 */
#define APP_STATE_CLEAR(_w) ({ \
    k_delayed_work_cancel(_w); \
})

/**
 * @def APP_STATE_EXTEND(_w, _t)
 * @brief Postpone pending states on worker-queue _w with time _t
 */
#define APP_STATE_EXTEND(_w, _t) ({ \
    k_delayed_work_submit(_w, _t); \
})

/**
 * @def APP_STATE_REMAINING(_w,_t)
 * @brief Retrieving remaining time _t of next state on worker-queue _w
 *
 * Note that time _t is returned by reference.
 */
#define APP_STATE_REMAINING(_w,_t) ({ \
    *_t = k_delayed_work_remaining_get(_w); \
})

/**
 * @def APP_STATE_NEXT_WQ(_s, _t)
 * @brief Schedule next state _s in time _t.
 *
 * The used worker queue is implicitly provided by the caller.
 */
#define APP_STATE_NEXT_WQ(_s, _t) ({ \
    struct k_delayed_work *_w = CONTAINER_OF(work, struct k_delayed_work, work);\
    APP_STATE_NEXT(_w, _s, _t); \
})

/**
 * @def APP_STATE_NOW_WQ(_s)
 * @brief Schedule next state _s now
 *
 * The used worker queue is implicitly provided by the caller.
 */
#define APP_STATE_NOW_WQ(_s) ({ \
    struct k_delayed_work *_w = CONTAINER_OF(work, struct k_delayed_work, work);\
    APP_STATE_NOW(_w, _s); \
})

/**
 * @def APP_STATE_CLEAR_WQ()
 * @brief Cancel any pending states on worker-queue of the caller.
 */
#define APP_STATE_CLEAR_WQ() ({ \
    struct k_delayed_work *_w = CONTAINER_OF(work, struct k_delayed_work, work);\
    APP_STATE_CLEAR(_w); \
})

/**
 * @def APP_STATE_EXTEND_WQ(_t)
 * @brief Postpone pending states with time _t
 *
 * The used worker queue is implicitly provided by the caller.
 */
#define APP_STATE_EXTEND_WQ(_t) ({ \
    struct k_delayed_work *_w = CONTAINER_OF(work, struct k_delayed_work, work);\
    APP_STATE_EXTEND_WQ(_w, _t); \
})

/**
 * @def APP_STATE_REMAINING_WQ_t)
 * @brief Retrieving remaining time _t of next state
 *
 * Note that time _t is returned by reference.
 * The used worker queue is implicitly provided by the caller.
 */ 
#define APP_STATE_REMAINING_WQ(_t) ({ \
    struct k_delayed_work *_w = CONTAINER_OF(work, struct k_delayed_work, work);\
    APP_STATE_EXTEND_WQ(_w,_t); \
})

#endif /* __CT_APP_STATES_H */
