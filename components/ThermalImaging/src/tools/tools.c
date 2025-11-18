#include "sdkconfig.h"
#include "tools.h"
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>

#if defined(CONFIG_SPIRAM) || defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_ESP32S3_SPIRAM_SUPPORT)
#define THERMALIMAGING_HAS_SPIRAM 1
#include <esp_psram.h>
#else
#define THERMALIMAGING_HAS_SPIRAM 0
#endif

void printfESPChipInfo(void)
{
    // 显示有关 CPU 的信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    printf("ESP32 chip model %d revision %d (%d CPU cores, WiFi%s%s)\n", chip_info.model, chip_info.revision, chip_info.cores, (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "", (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }
    printf("%lu MB %s SPI FLASH\n", (unsigned long)(flash_size / (1024 * 1024)), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

#if THERMALIMAGING_HAS_SPIRAM
    printf("%d MB SPI PSRAM\n", esp_psram_get_size() / (1024 * 1024));
    printf("Available Memory:\r\n");
    printf("Internal Memory: %d K\r\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024 / 8);
    printf("External Memory: %d K\r\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024 / 8);
#endif
}
