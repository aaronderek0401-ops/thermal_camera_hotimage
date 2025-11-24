#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <stdio.h>

// SIQ-02FVS3 旋转编码器测试
// - EC_A / EC_B 采用数字正交解码
// - 按键（SW）使用独立 GPIO（默认 GPIO8，低电平=按下）

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
#define ENC_STEPS_PER_DETENT 4 // 4 次状态变化视为一次有效旋转
#define ENC_CENTER_IDLE_TICKS 50 // ~100ms 无变化即回到 CENTER

static const int8_t s_quadrature_table[16] = {
    0, -1, +1, 0,
    +1, 0, 0, -1,
    -1, 0, 0, +1,
    0, +1, -1, 0
};

static const char *dir_to_string(int dir)
{
    if (dir > 0) return "RIGHT";
    if (dir < 0) return "LEFT";
    return "CENTER";
}

void siq02_task(void *arg)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << ENC_GPIO_A) |
                        (1ULL << ENC_GPIO_B) |
                        (1ULL << ENC_GPIO_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    printf("siq02: Rotary encoder A=%d B=%d SW=%d (pull-up, low active)\n",
           ENC_GPIO_A, ENC_GPIO_B, ENC_GPIO_SW);

    uint8_t prev_state = ((gpio_get_level(ENC_GPIO_A) & 1) << 1) |
                         (gpio_get_level(ENC_GPIO_B) & 1);
    int accum = 0;
    int current_dir = 0; // -1=LEFT, +1=RIGHT, 0=CENTER
    int idle_ticks = 0;

    bool last_press = (gpio_get_level(ENC_GPIO_SW) == 0);
    printf("Wheel: DIR=%s | PRESS=%s\n",
           dir_to_string(current_dir), last_press ? "DOWN" : "UP");

    while (1) {
        uint8_t curr_state = ((gpio_get_level(ENC_GPIO_A) & 1) << 1) |
                              (gpio_get_level(ENC_GPIO_B) & 1);
        uint8_t idx = (prev_state << 2) | curr_state;
        int8_t delta = s_quadrature_table[idx & 0x0F];
        prev_state = curr_state;

        bool dir_changed = false;
        if (delta != 0) {
            accum += delta;
            idle_ticks = 0;
            if (accum >= ENC_STEPS_PER_DETENT) {
                current_dir = +1;
                accum = 0;
                dir_changed = true;
            } else if (accum <= -ENC_STEPS_PER_DETENT) {
                current_dir = -1;
                accum = 0;
                dir_changed = true;
            }
        } else {
            if (idle_ticks < ENC_CENTER_IDLE_TICKS) idle_ticks++;
            if (idle_ticks >= ENC_CENTER_IDLE_TICKS && current_dir != 0) {
                current_dir = 0;
                dir_changed = true;
            }
        }

        bool pressed = (gpio_get_level(ENC_GPIO_SW) == 0);
        bool press_changed = (pressed != last_press);
        if (press_changed) {
            last_press = pressed;
        }

        if (dir_changed || press_changed) {
            printf("Wheel: DIR=%s | PRESS=%s\n",
                   dir_to_string(current_dir), pressed ? "DOWN" : "UP");
        }

        TickType_t delay_ticks = pdMS_TO_TICKS(ENC_SAMPLE_PERIOD_MS);
        if (delay_ticks == 0) delay_ticks = 1;
        vTaskDelay(delay_ticks);
    }
}

void start_siq02_test(void)
{
    xTaskCreate(siq02_task, "siq02", 2048, NULL, tskIDLE_PRIORITY+2, NULL);
}

