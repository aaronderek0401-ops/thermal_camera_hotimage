#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <stdio.h>
#include "wheel.h"
#include "thermalimaging_simple.h"

// wheel module internal queue & callback
static QueueHandle_t s_wheel_queue = NULL;
static void (*s_wheel_callback)(wheel_event_t) = NULL;

BaseType_t wheel_post_event(wheel_event_t evt)
{
    if (s_wheel_queue == NULL) return pdFALSE;
    return xQueueSend(s_wheel_queue, &evt, 0);
}

BaseType_t wheel_get_event(wheel_event_t* out_evt, TickType_t ticks_to_wait)
{
    if (s_wheel_queue == NULL) return pdFALSE;
    return xQueueReceive(s_wheel_queue, out_evt, ticks_to_wait);
}

void wheel_register_callback(void (*cb)(wheel_event_t))
{
    s_wheel_callback = cb;
}

esp_err_t wheel_init(void)
{
    if (s_wheel_queue == NULL) {
        s_wheel_queue = xQueueCreate(8, sizeof(wheel_event_t));
        if (s_wheel_queue == NULL) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// Wheel ADC 测试任务（原 adc_gpio17_test.c 重命名）
// 只读取 ADC2 CH6（对应 IO17）并打印

static adc_oneshot_unit_handle_t test_adc1_handle = NULL;
static adc_cali_handle_t test_adc1_cali_handle = NULL;
static adc_oneshot_unit_handle_t test_adc2_handle = NULL;
static adc_cali_handle_t test_adc2_cali_handle = NULL;
static const adc_atten_t test_atten = ADC_ATTEN_DB_6;

static bool test_adc_cali_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    bool calibrated = false;
    esp_err_t ret;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    return calibrated;
}

void wheel_task(void *arg)
{
    esp_err_t err;

    // 初始化 ADC1 oneshot
    adc_oneshot_unit_init_cfg_t init_cfg1 = {
        .unit_id = ADC_UNIT_1,
    };
    err = adc_oneshot_new_unit(&init_cfg1, &test_adc1_handle);
    if (err != ESP_OK) {
        printf("adc test: adc1 oneshot_new_unit failed: %s\n", esp_err_to_name(err));
    }

    // 初始化 ADC2 oneshot
    adc_oneshot_unit_init_cfg_t init_cfg2 = {
        .unit_id = ADC_UNIT_2,
    };
    err = adc_oneshot_new_unit(&init_cfg2, &test_adc2_handle);
    if (err != ESP_OK) {
        printf("adc test: adc2 oneshot_new_unit failed: %s\n", esp_err_to_name(err));
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = test_atten,
    };

    bool do_cali1 = test_adc_cali_init(ADC_UNIT_1, test_atten, &test_adc1_cali_handle);
    if (do_cali1) printf("adc test: ADC1 calibration enabled\n"); else printf("adc test: ADC1 calibration disabled\n");

    bool do_cali2 = test_adc_cali_init(ADC_UNIT_2, test_atten, &test_adc2_cali_handle);
    if (do_cali2) printf("adc test: ADC2 calibration enabled\n"); else printf("adc test: ADC2 calibration disabled\n");

    // 只读取 ADC2 CH6（对应 IO17）并打印
    const adc_channel_t target_ch = ADC_CHANNEL_6; // CH6
    // 电压阈值（mV）
    const int V_LEFT = 1800;   // 左拨期望 ~1.8V
    const int V_RIGHT = 1500;  // 右拨期望 ~1.5V
    const int V_PRESS = 1000;  // 按下期望 ~1.0V
    const int THRESH_HIGH = (V_LEFT + V_RIGHT) / 2; // 1650
    const int THRESH_LOW = (V_RIGHT + V_PRESS) / 2; // 1250

    wheel_event_t last_evt = WHEEL_EVENT_NONE;

    // ensure module queue exists
    if (s_wheel_queue == NULL) {
        // best-effort init if caller didn't call wheel_init
        wheel_init();
    }

    while (1) {
        printf("--- ADC2 CH6 (IO17) reading ---\n");
        if (test_adc2_handle) {
            esp_err_t r = adc_oneshot_config_channel(test_adc2_handle, target_ch, &chan_cfg);
            if (r == ESP_OK) {
                int raw = 0;
                r = adc_oneshot_read(test_adc2_handle, target_ch, &raw);
                int voltage_mv = -1;
                if (r == ESP_OK) {
                    if (test_adc2_cali_handle) {
                        esp_err_t ret = adc_cali_raw_to_voltage(test_adc2_cali_handle, raw, &voltage_mv);
                        if (ret != ESP_OK) voltage_mv = -1;
                    }
                    printf("ADC2 CH6: raw=%d, mV=%d\n", raw, voltage_mv);

                    // 根据 mV 值判定状态：LEFT / RIGHT / PRESS
                    wheel_event_t detected = WHEEL_EVENT_NONE;
                    if (voltage_mv >= THRESH_HIGH) {
                        detected = WHEEL_EVENT_LEFT;
                    } else if (voltage_mv >= THRESH_LOW) {
                        detected = WHEEL_EVENT_RIGHT;
                    } else if (voltage_mv >= 0) {
                        // 低于 THRESH_LOW 视为按下（或其他低电平状态）
                        detected = WHEEL_EVENT_PRESS;
                    }

                    // 仅在状态变化时发送/打印一次（避免重复刷屏）
                    if (detected != WHEEL_EVENT_NONE && detected != last_evt) {
                        const char *name = "NONE";
                        if (detected == WHEEL_EVENT_LEFT) name = "LEFT (Back)";
                        else if (detected == WHEEL_EVENT_RIGHT) name = "RIGHT (Confirm)";
                        else if (detected == WHEEL_EVENT_PRESS) name = "PRESS (Confirm)";
                        printf("Wheel detected: %s (mV=%d)\n", name, voltage_mv);
                        
                        // 直接设置渲染事件位：左拨=返回，右拨=确认，按下=确认
                        if (pHandleEventGroup != NULL) {
                            uint32_t event_bit = 0;
                            if (detected == WHEEL_EVENT_LEFT) event_bit = RENDER_Wheel_Back;
                            else if (detected == WHEEL_EVENT_RIGHT) event_bit = RENDER_Wheel_Confirm;
                            else if (detected == WHEEL_EVENT_PRESS) event_bit = RENDER_Wheel_Press;
                            if (event_bit) xEventGroupSetBits(pHandleEventGroup, event_bit);
                        }
                        
                        // 仍然发布到自己的队列和回调（保持 API 兼容性）
                        wheel_post_event(detected);
                        if (s_wheel_callback) s_wheel_callback(detected);
                        last_evt = detected;
                    }
                } else {
                    printf("ADC2 CH6 read failed: %s\n", esp_err_to_name(r));
                }
            } else {
                printf("ADC2 CH6 config failed: %s\n", esp_err_to_name(r));
            }
        } else {
            printf("ADC2 not initialized\n");
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// helper to start the wheel test task
void start_wheel_task()
{
    xTaskCreate(wheel_task, "wheel_task", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
}
