// wheel.h -- wheel (ADC-based selector) event API
#ifndef WHEEL_H
#define WHEEL_H

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WHEEL_EVENT_NONE = 0,
    WHEEL_EVENT_LEFT,
    WHEEL_EVENT_RIGHT,
    WHEEL_EVENT_PRESS
} wheel_event_t;

/**
 * Initialize wheel module (creates internal queue). Call before start_wheel_task
 * if you want to receive events. Returns ESP_OK on success.
 */
esp_err_t wheel_init(void);

/**
 * Start the wheel task (keeps existing helper API). This will not fail if
 * wheel_init has already been called.
 */
void start_wheel_task(void);

/**
 * Receive one wheel event from the internal queue.
 * out_evt: pointer to receive event
 * ticks_to_wait: ticks to wait (0 = non-blocking, portMAX_DELAY = block indefinitely)
 * Returns pdTRUE when an event was received.
 */
BaseType_t wheel_get_event(wheel_event_t* out_evt, TickType_t ticks_to_wait);

/**
 * Register a callback that will be invoked from the wheel task context when
 * an event is produced. Pass NULL to unregister.
 */
void wheel_register_callback(void (*cb)(wheel_event_t evt));

/**
 * Post an event into the wheel internal queue from other tasks.
 * Returns pdTRUE if posted, pdFALSE otherwise.
 */
BaseType_t wheel_post_event(wheel_event_t evt);

#ifdef __cplusplus
}
#endif

#endif // WHEEL_H
