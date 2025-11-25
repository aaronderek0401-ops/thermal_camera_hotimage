// siq02.h -- simple rotary encoder event API
#ifndef SIQ02_H
#define SIQ02_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIQ02_EVENT_NONE = 0,
    SIQ02_EVENT_LEFT,
    SIQ02_EVENT_RIGHT,
    SIQ02_EVENT_PRESS
} siq02_event_t;

/**
 * Initialize and start the siq02 encoder task.
 * Pass gpio numbers (gpio_num_t). Use -1 to accept compile-time defaults.
 * Returns ESP_OK on success.
 */
esp_err_t siq02_init(gpio_num_t a, gpio_num_t b, gpio_num_t sw);

/**
 * Backwards-compatible starter (keeps existing name used elsewhere).
 */
void start_siq02_test(void);

/**
 * Receive one encoder event from the internal queue.
 * out_evt: pointer to receive event
 * ticks_to_wait: ticks to wait (0 = non-blocking, portMAX_DELAY = block indefinitely)
 * Returns pdTRUE when an event was received.
 */
BaseType_t siq02_get_event(siq02_event_t* out_evt, TickType_t ticks_to_wait);

/**
 * Register a callback that will be invoked from the encoder task context when
 * an event is produced. Pass NULL to unregister.
 */
void siq02_register_callback(void (*cb)(siq02_event_t evt));


#ifdef __cplusplus
}
#endif

#endif // SIQ02_H
