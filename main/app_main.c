#include "thermalimaging_simple.h"
#include "tools/tools.h"
#include "iic.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include "wheel.h"
#include "siq02.h"

EventGroupHandle_t pHandleEventGroup = NULL;
SemaphoreHandle_t pSPIMutex = NULL;

void app_main()
{
    pSPIMutex = xSemaphoreCreateMutex();

    gpio_hold_dis(GPIO_NUM_5);

    // 初始化显示
    dispcolor_Init();

    // 初始化I2C总线 (MLX90640热成像传感器需要)
    I2CInit(CONFIG_IIC_IONUM_SDA, CONFIG_IIC_IONUM_SCL, CONFIG_ESP32_IIC_CLOCK, I2C_MODE_MASTER);

    printfESPChipInfo();

    // 初始化配置
    int err = settings_storage_init();
    settings_read_all();
    if (err) {
        console_printf(MsgWarning, "Error Reading Settings !\r\n");
    }

    // 设置LCD背光
    dispcolor_SetBrightness(settingsParms.LcdBrightness);

    // 创建任务
    pHandleEventGroup = xEventGroupCreate();

    // 启动热成像相关任务
    xTaskCreatePinnedToCore(render_task, "render", 1024 * 20, NULL, 5, NULL, 1);
    // buttons_task disabled - wheel and siq02 now generate events directly
    // xTaskCreatePinnedToCore(buttons_task, "buttons", 1024, NULL, 6, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(mlx90640_task, "mlx90640", 1024 * 10, NULL, 4, NULL, 0);

    // 启动 Wheel (IO17) ADC 测试任务（打印 ADC2 CH6 的 raw 与 mV）
    if (wheel_init() == ESP_OK) {
        start_wheel_task();
    } else {
        printf("wheel_init failed\n");
    }
    // 启动 SIQ-02FVS3 摇杆解析测试任务（X/Y/Press）
    extern void start_siq02_test(void);
    start_siq02_test();

    while (1) {
        vTaskDelay(60 * 1000 / portTICK_PERIOD_MS);
        // printf("Internal Memory: %d K\r\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024 / 8);
        // printf("External Memory: %d K\r\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024 / 8);
    }
}
