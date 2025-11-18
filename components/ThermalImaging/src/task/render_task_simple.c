#include "thermalimaging_simple.h"
#include "dispcolor.h"
#include "st7789.h"
#include "palette.h"
#include "IDW.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <stdbool.h>

#define TEMP_SCALE 10  // 温度放大倍数，与render_task.c一致

static bool compute_temp_range(const sMlxData* frame, float* minTemp, float* maxTemp)
{
    const int pixelCount = THERMALIMAGE_RESOLUTION_WIDTH * THERMALIMAGE_RESOLUTION_HEIGHT;
    bool hasValidSample = false;
    float localMin = FLT_MAX;
    float localMax = -FLT_MAX;

    for (int i = 0; i < pixelCount; i++) {
        float sample = frame->ThermoImage[i];
        if (!isfinite(sample)) {
            continue;
        }

        if (!hasValidSample) {
            localMin = localMax = sample;
            hasValidSample = true;
            continue;
        }

        if (sample < localMin) {
            localMin = sample;
        }
        if (sample > localMax) {
            localMax = sample;
        }
    }

    if (!hasValidSample) {
        return false;
    }

    *minTemp = localMin;
    *maxTemp = localMax;
    return true;
}

// 高质量图像绘制函数 - 参考render_task.c的DrawHQImage
static void DrawHQImage(int16_t* pImage, tRGBcolor* pPalette, uint16_t PaletteSize, 
                       uint16_t X, uint16_t Y, uint16_t width, uint16_t height, float minTemp)
{
    int cnt = 0;

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int16_t colorIdx = pImage[cnt] - (int16_t)(minTemp * TEMP_SCALE);
            cnt++;

            if (colorIdx < 0) {
                colorIdx = 0;
            } else if (colorIdx >= PaletteSize) {
                colorIdx = PaletteSize - 1;
            }

            uint16_t color = RGB565(pPalette[colorIdx].r, pPalette[colorIdx].g, pPalette[colorIdx].b);
            dispcolor_DrawPixel((width - col - 1) + X, row + Y, color);
        }
    }
}

// 外部变量声明 (来自mlx90640_task.c)
extern sMlxData* pMlxData;
extern EventGroupHandle_t pHandleEventGroup;

// 帧率计算
static uint32_t frame_count = 0;
static TickType_t last_fps_time = 0;
static float actual_fps = 0.0f;

// 图像缓冲区 - 参考render_task.c的优化
static int16_t* TermoImage16 = NULL;  // 热成像整数缓冲区（温度*10）
static int16_t* TermoHqImage16 = NULL; // 高质量插值后的图像
static float* gaussBuff = NULL;        // 高斯模糊缓冲区
static tRGBcolor* pPaletteImage = NULL;  // 伪彩色调色板

// 简化版渲染任务 - 使用render_task的图像优化方法
void render_task(void* arg)
{
    TickType_t perf_start, perf_mlx_read, perf_compute, perf_render, perf_lcd;
    
    printf("Render task started for thermal imaging display\n");
    
    // 分配图像缓冲区 - 参考render_task.c
    const int pixelCount = THERMALIMAGE_RESOLUTION_WIDTH * THERMALIMAGE_RESOLUTION_HEIGHT;
    const uint16_t hq_img_width = 192;   // 热成像显示宽度
    const uint16_t hq_img_height = 160;  // 热成像显示高度
    
    TermoImage16 = heap_caps_malloc(pixelCount * sizeof(int16_t), MALLOC_CAP_8BIT);
    TermoHqImage16 = heap_caps_malloc((hq_img_width * hq_img_height) * sizeof(int16_t), MALLOC_CAP_8BIT);
    gaussBuff = heap_caps_malloc(((THERMALIMAGE_RESOLUTION_WIDTH * 2) * (THERMALIMAGE_RESOLUTION_HEIGHT * 2)) * sizeof(float), MALLOC_CAP_8BIT);
    pPaletteImage = heap_caps_malloc((MAX_TEMP - MIN_TEMP) * TEMP_SCALE * sizeof(tRGBcolor), MALLOC_CAP_8BIT);
    
    if (!TermoImage16 || !TermoHqImage16 || !gaussBuff || !pPaletteImage) {
        printf("Failed to allocate image buffers!\n");
        vTaskDelete(NULL);
        return;
    }
    
    printf("Image buffers allocated successfully\n");
    
    // 等待MLX90640初始化完成
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // 主循环
    while (1) {
        // 等待MLX90640数据更新
        EventBits_t uxBitsToWaitFor = RENDER_MLX90640_NO0 | RENDER_MLX90640_NO1;
        EventBits_t bits = xEventGroupWaitBits(pHandleEventGroup, uxBitsToWaitFor, pdTRUE, pdFALSE, portMAX_DELAY);
        
        if ((bits & RENDER_MLX90640_NO0) == RENDER_MLX90640_NO0 || (bits & RENDER_MLX90640_NO1) == RENDER_MLX90640_NO1) {
            perf_start = xTaskGetTickCount();

            // 计算实际帧率
            frame_count++;
            TickType_t current_time = xTaskGetTickCount();
            TickType_t render_start = current_time;  // 记录渲染开始时间
            
            if (last_fps_time == 0) {
                last_fps_time = current_time;
            } else {
                TickType_t time_diff = current_time - last_fps_time;
                if (time_diff >= pdMS_TO_TICKS(1000)) { // 每秒更新一次FPS
                    actual_fps = (float)frame_count * 1000.0f / pdTICKS_TO_MS(time_diff);
                    frame_count = 0;
                    last_fps_time = current_time;
                }
            }

            if (pMlxData == NULL) {
                printf("MLX90640 data not available\n");
                continue;
            }

            // Use whichever frame buffer the MLX task just filled
            int bufferIndex = (bits & RENDER_MLX90640_NO1) ? 1 : 0;
            sMlxData* frame = &pMlxData[bufferIndex];
            perf_mlx_read = xTaskGetTickCount();

            // 计算温度范围
            float minTemp = MIN_TEMP;
            float maxTemp = MIN_TEMP + 1;
            if (!compute_temp_range(frame, &minTemp, &maxTemp)) {
                printf("Invalid MLX90640 temperature frame\n");
                continue;
            }

            float tempRange = maxTemp - minTemp;
            if (tempRange < 0.001f) {
                tempRange = 1.0f; // 避免除零与极小范围
            }
            
            // 生成伪彩色调色板 - 参考render_task.c的RedrawPalette
            uint16_t paletteSteps = (uint16_t)(tempRange * TEMP_SCALE);
            if (paletteSteps == 0) paletteSteps = 1;
            getPalette(settingsParms.ColorScale, paletteSteps, pPaletteImage);
            
            // 将温度转换为整数数组 - 参考render_task.c
            const int pixelCount = THERMALIMAGE_RESOLUTION_WIDTH * THERMALIMAGE_RESOLUTION_HEIGHT;
            for (int i = 0; i < pixelCount; i++) {
                TermoImage16[i] = (int16_t)(frame->ThermoImage[i] * TEMP_SCALE);
            }
            
            perf_compute = xTaskGetTickCount();

            // 清除文字显示区域，避免重叠
            dispcolor_FillRect(0, 0, 240, 45, BLACK);      // 顶部标题区域
            dispcolor_FillRect(0, 205, 240, 35, BLACK);    // 底部信息区域
            
            // 显示标题
            dispcolor_DrawString(10, 10, FONTID_24F, (uint8_t*)"ESP32S3 Thermal Camera", WHITE);
            
            // 使用高斯模糊+双线性插值优化 - 参考render_task.c的HQ3X_2X模式
            // 计算热成像显示区域 (留出顶部45和底部35像素)
            const uint16_t img_y_start = 45;
            const uint16_t img_height = 205 - 45;  // 160像素高度
            const uint16_t img_width = 192;        // 保持32x24的宽高比 (160*32/24≈213，取192居中)
            const uint16_t img_x_start = (240 - img_width) / 2;  // 居中显示
            
            // 步骤1: 高斯模糊2倍放大
            idwGauss(TermoImage16, THERMALIMAGE_RESOLUTION_WIDTH, THERMALIMAGE_RESOLUTION_HEIGHT, 2, gaussBuff);
            
            // 步骤2: 双线性插值到指定区域
            idwBilinear(gaussBuff, THERMALIMAGE_RESOLUTION_WIDTH * 2, THERMALIMAGE_RESOLUTION_HEIGHT * 2, 
                       TermoHqImage16, img_width, img_height, 10 / 2);
            
            // 步骤3: 绘制高质量图像到指定区域
            DrawHQImage(TermoHqImage16, pPaletteImage, paletteSteps, img_x_start, img_y_start, 
                       img_width, img_height, minTemp);
            perf_render = xTaskGetTickCount();
            
            // 显示温度范围信息和帧率
            dispcolor_printf(10, 210, FONTID_16F, WHITE, "Min:%.1fC Max:%.1fC", minTemp, maxTemp);
            if (actual_fps > 0.0f) {
                dispcolor_printf(10, 225, FONTID_16F, WHITE, "Actual: %.1f FPS (Set: %.1f)", 
                    actual_fps, FPS_RATES[settingsParms.MLX90640FPS]);
            } else {
                dispcolor_printf(10, 225, FONTID_16F, WHITE, "FPS: %.1f (measuring...)", 
                    FPS_RATES[settingsParms.MLX90640FPS]);
            }
            
            // 更新显示
            st7789_update();
            perf_lcd = xTaskGetTickCount();
            
            // 性能分析输出
            if (frame_count % 30 == 0) { // 每30帧输出一次
                uint32_t total_ms = pdTICKS_TO_MS(perf_lcd - perf_start);
                uint32_t mlx_ms = pdTICKS_TO_MS(perf_mlx_read - perf_start);
                uint32_t compute_ms = pdTICKS_TO_MS(perf_compute - perf_mlx_read);
                uint32_t render_ms = pdTICKS_TO_MS(perf_render - perf_compute);
                uint32_t lcd_ms = pdTICKS_TO_MS(perf_lcd - perf_render);
                
                printf("FPS: %.2f | Total: %lums | MLX: %lums | Compute: %lums | Render: %lums | LCD: %lums\n", 
                    actual_fps, (unsigned long)total_ms, (unsigned long)mlx_ms, 
                    (unsigned long)compute_ms, (unsigned long)render_ms, (unsigned long)lcd_ms);
            }
        }
        
        // 不需要额外延迟，xEventGroupWaitBits已经会等待新数据
        // vTaskDelay(50 / portTICK_PERIOD_MS);  // 移除：这个延迟导致FPS被限制
    }
}