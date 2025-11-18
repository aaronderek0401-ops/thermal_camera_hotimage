#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"
#include "thermalimaging.h"
#include <string.h>

#define AVERAGE_ADC_CHARGE 5 // 是否充电
#define AVERAGE_ADC_BATVOL 64 // 电池电压

static SAFilterHandle_t* pFilter_ADC_charge = NULL; // 是否充电
static SAFilterHandle_t* pFilter_ADC_vol = NULL; // 电池电压

#define UPPER_DIVIDER 442 // 电阻�?#define LOWER_DIVIDER 160 // 电阻�?#define DEFAULT_VREF 1100 // Use adc2_vref_to_gpio() to obtain a better estimate

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;
static const adc_channel_t CHANNEL_BATCHARGE = ADC_CHANNEL_6; // GPIO34 ADC1 CHANNEL6 是否充电
static const adc_channel_t CHANNEL_BATVOL = ADC_CHANNEL_7; // GPIO35 ADC1 CHANNEL7 电池电压
static const adc_atten_t atten = ADC_ATTEN_DB_6;

// ADC校准初始�?static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
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
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    return calibrated;
}

// 获得电池电压
uint32_t getBatteryVoltage()
{
    if (NULL == adc1_cali_handle) {
        return 0;
    }
    float adc_reading = GetSAFiterRes(pFilter_ADC_vol);

    // 电池电压计算
    int voltage_mv;
    esp_err_t ret = adc_cali_raw_to_voltage(adc1_cali_handle, (int)adc_reading, &voltage_mv);
    if (ret == ESP_OK) {
        uint32_t voltage = voltage_mv * (LOWER_DIVIDER + UPPER_DIVIDER) / LOWER_DIVIDER;
        return voltage;
    }
    return 0;
}

// 判断是否充电�?int8_t getBatteryCharge()
{
    if (NULL == pFilter_ADC_charge) {
        return -1;
    }
    float adc_reading = GetSAFiterRes(pFilter_ADC_charge);
    return (int8_t)adc_reading;
}

/**
 * @brief 进行一次AD采样转换
 *
 * @return float
 */
static float ADCGetVol()
{
    int adc_raw;
    esp_err_t ret = adc_oneshot_read(adc1_handle, CHANNEL_BATVOL, &adc_raw);
    if (ret == ESP_OK) {
        return (float)adc_raw;
    }
    return 0.0f;
}

static float ADCGetCharge()
{
    int adc_raw;
    esp_err_t ret = adc_oneshot_read(adc1_handle, CHANNEL_BATCHARGE, &adc_raw);
    if (ret == ESP_OK) {
        return (float)adc_raw;
    }
    return 0.0f;
}

/**
 * @brief 初始化ADC外设
 *
 * @return esp_err_t
 */
static esp_err_t init_adc()
{
    esp_err_t err = ESP_OK;
    
    // ADC1 初始�?    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    
    err = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (err != ESP_OK) {
        printf("ADC1 init failed: %s\n", esp_err_to_name(err));
        return err;
    }
    
    // ADC1 通道配置
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = atten,
    };
    
    err = adc_oneshot_config_channel(adc1_handle, CHANNEL_BATVOL, &config);
    if (err != ESP_OK) {
        printf("ADC1 channel %d config failed: %s\n", CHANNEL_BATVOL, esp_err_to_name(err));
        return err;
    }
    
    err = adc_oneshot_config_channel(adc1_handle, CHANNEL_BATCHARGE, &config);
    if (err != ESP_OK) {
        printf("ADC1 channel %d config failed: %s\n", CHANNEL_BATCHARGE, esp_err_to_name(err));
        return err;
    }
    
    // ADC 校准初始�?    bool do_calibration = adc_calibration_init(ADC_UNIT_1, atten, &adc1_cali_handle);
    if (do_calibration) {
        printf("ADC calibration enabled\n");
    } else {
        printf("ADC calibration disabled\n");
    }
    
    return err;
}

void adc_task(void* arg)
{
    // ADC 初始�?    if (ESP_OK != init_adc()) {
        goto error;
    }

    pFilter_ADC_vol = SlipAveFilterCreate(AVERAGE_ADC_BATVOL);
    pFilter_ADC_charge = SlipAveFilterCreate(AVERAGE_ADC_CHARGE);

    // 是否充电
    for (uint16_t i = 0; i < AVERAGE_ADC_CHARGE; i++) {
        AddSAFiterRes(pFilter_ADC_charge, ADCGetVol());
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // 当前电池电压
    for (uint16_t i = 0; i < AVERAGE_ADC_BATVOL; i++) {
        AddSAFiterRes(pFilter_ADC_vol, ADCGetVol());
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    while (1) {
        AddSAFiterRes(pFilter_ADC_vol, ADCGetVol());
        AddSAFiterRes(pFilter_ADC_charge, ADCGetCharge());
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

error:
    printf("Error ADC init Tasks\r\n");
}

// TODO ESP32好像不支持DMA方式读取ADC的值？
