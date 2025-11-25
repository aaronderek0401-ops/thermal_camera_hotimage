// 修改后：将旋钮封装为事件接口（LEFT/RIGHT/PRESS），支持队列与回调
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "siq02.h"
#include "thermalimaging_simple.h"
#include <stdio.h>
#include <math.h>

// 默认 GPIO（可在 siq02_init 中覆盖）
#ifndef ENC_GPIO_A
#define ENC_GPIO_A GPIO_NUM_17
#endif
#ifndef ENC_GPIO_B
#define ENC_GPIO_B GPIO_NUM_18
#endif
#ifndef ENC_GPIO_SW
#define ENC_GPIO_SW GPIO_NUM_8
#endif

#define ENC_SAMPLE_PERIOD_MS 2
#define ENC_STEPS_PER_DETENT 4
#define ENC_CENTER_IDLE_TICKS 50
#ifndef ENC_DETENTS_PER_REV
#define ENC_DETENTS_PER_REV 20
#endif

static const int8_t s_quadrature_table[16] = {
    0, -1, +1, 0,
    +1, 0, 0, -1,
    -1, 0, 0, +1,
    0, +1, -1, 0
};

// 内部状态
static QueueHandle_t s_siq02_queue = NULL;
static void (*s_siq02_callback)(siq02_event_t) = NULL;
static gpio_num_t s_gpio_a = ENC_GPIO_A;
static gpio_num_t s_gpio_b = ENC_GPIO_B;
static gpio_num_t s_gpio_sw = ENC_GPIO_SW;

static void siq02_task(void *arg)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << (int)s_gpio_a) |
                        (1ULL << (int)s_gpio_b) |
                        (1ULL << (int)s_gpio_sw),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    uint8_t prev_state = ((gpio_get_level(s_gpio_a) & 1) << 1) |
                         (gpio_get_level(s_gpio_b) & 1);
    int accum = 0;
    int current_dir = 0;
    int idle_ticks = 0;
    int position = 0;

    bool last_press = (gpio_get_level(s_gpio_sw) == 0);

    while (1) {
        uint8_t curr_state = ((gpio_get_level(s_gpio_a) & 1) << 1) |
                              (gpio_get_level(s_gpio_b) & 1);
        uint8_t idx = (prev_state << 2) | curr_state;
        int8_t delta = s_quadrature_table[idx & 0x0F];
        prev_state = curr_state;

        siq02_event_t evt = SIQ02_EVENT_NONE;

        if (delta != 0) {
            accum += delta;
            idle_ticks = 0;
            if (accum >= ENC_STEPS_PER_DETENT) {
                current_dir = +1;
                accum = 0;
                position += current_dir;
                evt = SIQ02_EVENT_RIGHT;
            } else if (accum <= -ENC_STEPS_PER_DETENT) {
                current_dir = -1;
                accum = 0;
                position += current_dir;
                evt = SIQ02_EVENT_LEFT;
            }
        } else {
            if (idle_ticks < ENC_CENTER_IDLE_TICKS) idle_ticks++;
            if (idle_ticks >= ENC_CENTER_IDLE_TICKS && current_dir != 0) {
                current_dir = 0;
            }
        }

        bool pressed = (gpio_get_level(s_gpio_sw) == 0);
        bool press_down = (!last_press && pressed);
        last_press = pressed;
        if (press_down) {
            evt = SIQ02_EVENT_PRESS;
        }

        if (evt != SIQ02_EVENT_NONE) {
            // 直接设置渲染事件位：左转=向上/减少，右转=向下/增加，按下=确认
            if (pHandleEventGroup != NULL) {
                uint32_t event_bit = 0;
                if (evt == SIQ02_EVENT_LEFT) event_bit = RENDER_Encoder_Up;
                else if (evt == SIQ02_EVENT_RIGHT) event_bit = RENDER_Encoder_Down;
                else if (evt == SIQ02_EVENT_PRESS) event_bit = RENDER_Encoder_Press;
                if (event_bit) xEventGroupSetBits(pHandleEventGroup, event_bit);
            }
            
            // 仍然发布到自己的队列和回调（保持 API 兼容性）
            if (s_siq02_queue) {
                xQueueSend(s_siq02_queue, &evt, 0);
            }
            if (s_siq02_callback) {
                s_siq02_callback(evt);
            }
        }

        TickType_t delay_ticks = pdMS_TO_TICKS(ENC_SAMPLE_PERIOD_MS);
        if (delay_ticks == 0) delay_ticks = 1;
        vTaskDelay(delay_ticks);
    }
}

esp_err_t siq02_init(gpio_num_t a, gpio_num_t b, gpio_num_t sw)
{
    s_gpio_a = (a == -1) ? ENC_GPIO_A : a;
    s_gpio_b = (b == -1) ? ENC_GPIO_B : b;
    s_gpio_sw = (sw == -1) ? ENC_GPIO_SW : sw;

    if (s_siq02_queue == NULL) {
        s_siq02_queue = xQueueCreate(8, sizeof(siq02_event_t));
        if (s_siq02_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    BaseType_t ok = xTaskCreate(siq02_task, "siq02", 2048, NULL, tskIDLE_PRIORITY + 2, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

void start_siq02_test(void)
{
    siq02_init(ENC_GPIO_A, ENC_GPIO_B, ENC_GPIO_SW);
}

BaseType_t siq02_get_event(siq02_event_t* out_evt, TickType_t ticks_to_wait)
{
    if (s_siq02_queue == NULL) return pdFALSE;
    return xQueueReceive(s_siq02_queue, out_evt, ticks_to_wait);
}

void siq02_register_callback(void (*cb)(siq02_event_t))
{
    s_siq02_callback = cb;
}



