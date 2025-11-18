#include "thermalimaging_simple.h"
#include "dispcolor.h"
#include "st7789.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <stdbool.h>

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

// 外部变量声明 (来自mlx90640_task.c)
extern sMlxData* pMlxData;
extern EventGroupHandle_t pHandleEventGroup;

// 帧率计算
static uint32_t frame_count = 0;
static TickType_t last_fps_time = 0;
static float actual_fps = 0.0f;
// 性能分析
static TickType_t perf_start, perf_mlx_read, perf_compute, perf_render, perf_lcd;

// 简化版渲染任务 - 显示真实的MLX90640热成像数据
void render_task(void* arg)
{
    TickType_t perf_start, perf_mlx_read, perf_compute, perf_render, perf_lcd;
    
    printf("Render task started for thermal imaging display\n");
    
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
            perf_compute = xTaskGetTickCount();

            // 清屏 - 只清除需要的区域，而不是整个屏幕
            // st7789_FillRect(0, 0, 240, 240, BLACK);  // 旧的全屏清除
            
            // 清除标题区域
            st7789_FillRect(0, 0, 240, 40, BLACK);
            // 清除热成像图像区域
            int image_width = THERMALIMAGE_RESOLUTION_WIDTH * 6;
            int image_height = THERMALIMAGE_RESOLUTION_HEIGHT * 6;
            int image_start_x = (240 - image_width) / 2;
            st7789_FillRect(image_start_x, 50, image_width, image_height, BLACK);
            // 清除底部信息区域
            st7789_FillRect(0, 200, 240, 40, BLACK);
            
            // 显示标题
            dispcolor_DrawString(10, 10, FONTID_24F, (uint8_t*)"ESP32S3 Thermal Camera", WHITE);
            
            // 渲染热成像图像到显示屏 - 优化版本，直接操作像素
            int scale = 6; // 每个像素放大6倍 (32*6 = 192, 24*6 = 144)
            int start_x = (240 - THERMALIMAGE_RESOLUTION_WIDTH * scale) / 2;
            int start_y = 50;
            
            for (int y = 0; y < THERMALIMAGE_RESOLUTION_HEIGHT; y++) {
                for (int x = 0; x < THERMALIMAGE_RESOLUTION_WIDTH; x++) {
                    int index = y * THERMALIMAGE_RESOLUTION_WIDTH + x;
                    float temp = frame->ThermoImage[index];

                    // 温度到颜色的映射 (简单的热力图)
                    uint16_t color;
                    if (!isfinite(temp)) {
                        color = BLACK;
                    } else {
                        float normalized = (temp - minTemp) / tempRange;
                        if (normalized < 0.0f) {
                            normalized = 0.0f;
                        } else if (normalized > 1.0f) {
                            normalized = 1.0f;
                        }

                        if (normalized < 0.25f) {
                            color = BLUE; // 冷 - 蓝色
                        } else if (normalized < 0.5f) {
                            color = GREEN; // 温 - 绿色
                        } else if (normalized < 0.75f) {
                            color = YELLOW; // 热 - 黄色
                        } else {
                            color = RED; // 很热 - 红色
                        }
                    }
                    
                    // 优化: 直接使用DrawPixel绘制放大的像素块
                    // 这比FillRect快得多，因为避免了重复的边界检查
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            dispcolor_DrawPixel(start_x + x * scale + dx, 
                                              start_y + y * scale + dy, color);
                        }
                    }
                }
            }
            TickType_t perf_render = xTaskGetTickCount();
            
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
        
        // 短暂延时
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}