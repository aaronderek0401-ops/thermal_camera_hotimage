#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <stdio.h>

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
