#include "sleep.h"
#include "dispcolor.h"
#include "st7789.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
// #include "esp32/ulp.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

// static RTC_DATA_ATTR struct timeval sleep_enter_time;

/**
 * @brief ESP32进入睡眠模式
 *
 */
void Deep_Sleep_Run()
{
#if 0
    struct timeval now;
    gettimeofday(&now, NULL);

    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT1: {
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pin_mask != 0) {
            int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
            printf("Wake Up From GPIO%d\n", pin);
        } else {
            printf("Wake Up From GPIO\n");
        }
        break;
    }

    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        printf("Not Deep Sleep Reset\n");
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    const int ext_wakeup_pin = 38;
    const uint64_t ext_wakeup_pin_mask = 1ULL << ext_wakeup_pin;

    printf("In Pin GPIO%d Enable EXT1 Wakeup On\n", ext_wakeup_pin);
    esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_mask, ESP_EXT1_WAKEUP_ALL_LOW);

    // Isolate GPIO12 pin from external circuits. This is needed for modules
    // which have an external pull-up resistor on GPIO12 (such as ESP32-WROVER)
    // to minimize current consumption.
    rtc_gpio_isolate(GPIO_NUM_0);
    rtc_gpio_isolate(GPIO_NUM_12);
    rtc_gpio_isolate(GPIO_NUM_25);
    rtc_gpio_isolate(GPIO_NUM_34);
    rtc_gpio_isolate(GPIO_NUM_35);
    rtc_gpio_isolate(GPIO_NUM_36);

    dispcolor_SetBrightness(0);
    dispcolor_DisplayOff();
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    gpio_hold_en(GPIO_NUM_5);
    gpio_deep_sleep_hold_en();

    printf("Enter Deep Sleep\n");
    gettimeofday(&sleep_enter_time, NULL);

    esp_deep_sleep_start();
#endif
}

// -------------------------------------------------------------
// Runtime sleep manager (lightweight):
// - system_enter_sleep(): pause MLX reads, turn off/dim display, spawn watcher
// - system_exit_sleep(): resume MLX reads, restore display/backlight
// The watcher task waits for any user input event and wakes the system.
// -------------------------------------------------------------

#include "tasks/render_task.h" // for pHandleEventGroup and event masks
#include "settings.h"

// external hook to pause MLX reads (defined in mlx90640_task.c)
extern uint8_t setMLX90640IsPause(uint8_t isPause);

static volatile bool s_sleepMode = false;
static TaskHandle_t s_sleepWatcher = NULL;

// mask of input events that should wake the system
#define WAKE_INPUT_MASK (RENDER_Wheel_Back | RENDER_Wheel_Confirm | RENDER_Wheel_Press | \
                        RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Encoder_Press | \
                        RENDER_ShortPress_Center | RENDER_ShortPress_Up | RENDER_ShortPress_Down)

static void sleep_watcher_task(void* arg)
{
    (void)arg;
    if (pHandleEventGroup == NULL) {
        vTaskDelete(NULL);
        return;
    }

    // wait for any input event indefinitely
    EventBits_t bits = xEventGroupWaitBits(pHandleEventGroup, WAKE_INPUT_MASK, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & WAKE_INPUT_MASK) {
        // wake
        system_exit_sleep();
    }

    s_sleepWatcher = NULL;
    vTaskDelete(NULL);
}

void system_enter_sleep(void)
{
    // keep existing light-sleep style behaviour for quick suspend
    if (s_sleepMode) return;
    s_sleepMode = true;

    // pause MLX sampling
    setMLX90640IsPause(1);

    // dim backlight and send display off
    dispcolor_SetBrightness(0);
    dispcolor_DisplayOff();

    // start watcher task to listen for any input to wake (light-sleep style)
    if (s_sleepWatcher == NULL) {
        xTaskCreatePinnedToCore(sleep_watcher_task, "sleep_watcher", 1024, NULL, 5, &s_sleepWatcher, tskNO_AFFINITY);
    }
}

void system_exit_sleep(void)
{
    if (!s_sleepMode) return;
    s_sleepMode = false;

    // resume MLX sampling
    setMLX90640IsPause(0);

    // restore backlight using global settingsParms
    // Turn display module back on, then restore backlight
    st7789_DisplayOn();
    vTaskDelay(20 / portTICK_PERIOD_MS);
    dispcolor_SetBrightness(settingsParms.LcdBrightness);

    // request a render by setting an MLX frame bit so render loop will draw once MLX resumes
    if (pHandleEventGroup) {
        xEventGroupSetBits(pHandleEventGroup, RENDER_MLX90640_NO0);
    }
}

// -------------------------------
// Deep sleep helper
// -------------------------------
// Default wake pin (must be RTC-capable). Change if your board uses another pin.
#ifndef DEEP_SLEEP_WAKE_PIN
#define DEEP_SLEEP_WAKE_PIN GPIO_NUM_1
#endif

void system_enter_deep_sleep(void)
{
    // prepare for deep sleep: stop sensor, turn off display/backlight
    setMLX90640IsPause(1);
    dispcolor_SetBrightness(0);
    dispcolor_DisplayOff();

    // Ensure wake pin is input with pull-up (so button press pulls low to wake)
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << DEEP_SLEEP_WAKE_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Use ext0 wakeup on the selected RTC-capable pin: wake on low level
    esp_sleep_enable_ext0_wakeup(DEEP_SLEEP_WAKE_PIN, 0);

    // Optionally isolate some GPIOs to reduce leakage (board-specific)
    // rtc_gpio_isolate(GPIO_NUM_x);

    // Enter deep sleep; this function does not return
    esp_deep_sleep_start();
}