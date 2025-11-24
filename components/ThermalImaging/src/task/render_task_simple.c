#include "thermalimaging_simple.h"
#include "dispcolor.h"
#include "st7789.h"
#include "palette.h"
#include "IDW.h"
#include "CelsiusSymbol.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <stdbool.h>
#include "simple_menu.h"
#include "settings.h"

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

// 计算MLX90640的温度数据 - 参考render_task.c
static void CalcTempFromMLX90640(sMlxData* pMlxData)
{
    const float* pThermoImage = pMlxData->ThermoImage;

    // 计算屏幕中心的温度 累加中间4个像素的值
    pMlxData->CenterTemp = 
        pThermoImage[THERMALIMAGE_RESOLUTION_WIDTH * ((THERMALIMAGE_RESOLUTION_HEIGHT >> 1) - 1) + ((THERMALIMAGE_RESOLUTION_WIDTH >> 1) - 1)] +
        pThermoImage[THERMALIMAGE_RESOLUTION_WIDTH * ((THERMALIMAGE_RESOLUTION_HEIGHT >> 1) - 1) + (THERMALIMAGE_RESOLUTION_WIDTH >> 1)] +
        pThermoImage[THERMALIMAGE_RESOLUTION_WIDTH * (THERMALIMAGE_RESOLUTION_HEIGHT >> 1) + ((THERMALIMAGE_RESOLUTION_WIDTH >> 1) - 1)] +
        pThermoImage[THERMALIMAGE_RESOLUTION_WIDTH * (THERMALIMAGE_RESOLUTION_HEIGHT >> 1) + (THERMALIMAGE_RESOLUTION_WIDTH >> 1)];
    pMlxData->CenterTemp /= 4;

    // 搜索帧中的最小和最大温度 及坐标
    pMlxData->minT = MAX_TEMP;
    pMlxData->maxT = MIN_TEMP;

    for (uint8_t y = 0; y < THERMALIMAGE_RESOLUTION_HEIGHT; y++) {
        for (uint8_t x = 0; x < THERMALIMAGE_RESOLUTION_WIDTH; x++) {
            float temp = pThermoImage[y * THERMALIMAGE_RESOLUTION_WIDTH + x];

            if (pMlxData->maxT < temp) {
                pMlxData->maxT = temp;
                pMlxData->maxT_X = x;
                pMlxData->maxT_Y = y;
            }

            if (pMlxData->minT > temp) {
                pMlxData->minT = temp;
                pMlxData->minT_X = x;
                pMlxData->minT_Y = y;
            }
        }
    }

    if (pMlxData->maxT > MAX_TEMP) {
        pMlxData->maxT = MAX_TEMP;
    }

    if (pMlxData->minT < MIN_TEMP) {
        pMlxData->minT = MIN_TEMP;
    }
}

// 绘制中心温度显示 - 参考render_task.c
static void DrawCenterTempColor(uint16_t cX, uint16_t cY, float TempCelsius, uint16_t color, bool useFahrenheit)
{
    char str[32];
    float displayTemp = TempCelsius;
    const char* unitSymbol = CELSIUS_SYMBOL;
    
    // 如果使用华氏度，进行转换
    if (useFahrenheit) {
        displayTemp = TempCelsius * 9.0f / 5.0f + 32.0f;
        unitSymbol = FAHRENHEIT_SYMBOL;
    }
    
    sprintf(str, "%.1f%s", displayTemp, unitSymbol);
    
    int16_t strWidth = strlen(str) * 6;  // FONTID_6X8M字体宽度
    int16_t strHeight = 8;
    
    dispcolor_printf(cX - (strWidth >> 1), cY - (strHeight >> 1), FONTID_6X8M, color, "%s", str);
}

static void DrawCenterTemp(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, float CenterTemp, bool useFahrenheit)
{
    uint16_t cX = (Width >> 1) + X;
    uint16_t cY = (Height >> 1) + Y;

    // 渲染阴影黑色
    DrawCenterTempColor(cX + 1, cY + 1, CenterTemp, BLACK, useFahrenheit);
    // 渲染白色
    DrawCenterTempColor(cX, cY, CenterTemp, WHITE, useFahrenheit);
}

// 绘制最大最小温度标记 - 参考render_task.c的DrawMarkersHQ
static void DrawMarkersHQ(sMlxData* pMlxData, uint16_t img_x, uint16_t img_y, uint16_t img_w, uint16_t img_h)
{
    uint8_t lineHalf = 4;
    
    // 计算缩放比例
    float scaleX = (float)img_w / THERMALIMAGE_RESOLUTION_WIDTH;
    float scaleY = (float)img_h / THERMALIMAGE_RESOLUTION_HEIGHT;

    // 绘制最大温度标记 (红色十字)
    int16_t x = (THERMALIMAGE_RESOLUTION_WIDTH - pMlxData->maxT_X - 1) * scaleX + img_x;
    int16_t y = pMlxData->maxT_Y * scaleY + img_y;

    uint16_t mainColor = RED;
    dispcolor_DrawLine(x + 1, y - lineHalf + 1, x + 1, y + lineHalf + 1, BLACK);
    dispcolor_DrawLine(x - lineHalf + 1, y + 1, x + lineHalf + 1, y + 1, BLACK);
    dispcolor_DrawLine(x - lineHalf + 1, y - lineHalf + 1, x + lineHalf + 1, y + lineHalf + 1, BLACK);
    dispcolor_DrawLine(x - lineHalf + 1, y + lineHalf + 1, x + lineHalf + 1, y - lineHalf + 1, BLACK);

    dispcolor_DrawLine(x, y - lineHalf, x, y + lineHalf, mainColor);
    dispcolor_DrawLine(x - lineHalf, y, x + lineHalf, y, mainColor);
    dispcolor_DrawLine(x - lineHalf, y - lineHalf, x + lineHalf, y + lineHalf, mainColor);
    dispcolor_DrawLine(x - lineHalf, y + lineHalf, x + lineHalf, y - lineHalf, mainColor);

    // 绘制最小温度标记 (蓝色X)
    x = (THERMALIMAGE_RESOLUTION_WIDTH - pMlxData->minT_X - 1) * scaleX + img_x;
    y = pMlxData->minT_Y * scaleY + img_y;

    mainColor = RGB565(0, 200, 245);
    dispcolor_DrawLine(x - lineHalf + 1, y - lineHalf + 1, x + lineHalf + 1, y + lineHalf + 1, BLACK);
    dispcolor_DrawLine(x - lineHalf + 1, y + lineHalf + 1, x + lineHalf + 1, y - lineHalf + 1, BLACK);
    dispcolor_DrawLine(x - lineHalf, y - lineHalf, x + lineHalf, y + lineHalf, mainColor);
    dispcolor_DrawLine(x - lineHalf, y + lineHalf, x + lineHalf, y - lineHalf, mainColor);
}

// 高质量图像绘制函数 - 参考render_task.c的DrawHQImage
static void DrawHQImage(int16_t* pImage, tRGBcolor* pPalette, uint16_t PaletteSize, 
                       uint16_t X, uint16_t Y, uint16_t width, uint16_t height, float minTemp, float maxTemp)
{
    int cnt = 0;

    // Precompute values used for mapping to avoid per-pixel repeated work
    int16_t minS = (int16_t)(minTemp * TEMP_SCALE);
    int16_t maxS = (int16_t)(maxTemp * TEMP_SCALE);
    int paletteMid = PaletteSize / 2;
    int pc = settingsParms.PaletteCenterPercent; // 0..100
    int16_t centerS = (int16_t)((minTemp + (pc / 100.0f) * (maxTemp - minTemp)) * TEMP_SCALE);
    int denomLower = centerS - minS; // may be <=0
    int denomUpper = maxS - centerS; // may be <=0

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int16_t pixel = pImage[cnt++];

            int idx;
            if (centerS <= minS || centerS >= maxS) {
                // degenerate: fall back to linear mapping
                idx = (int)pixel - (int)minS;
            } else {
                if (pixel <= centerS) {
                    // Map [minS .. centerS] -> [0 .. paletteMid]
                    if (denomLower <= 0) {
                        idx = 0;
                    } else {
                        idx = ((int32_t)(pixel - minS) * paletteMid) / denomLower;
                    }
                } else {
                    // Map (centerS .. maxS] -> (paletteMid .. PaletteSize-1]
                    if (denomUpper <= 0) {
                        idx = paletteMid;
                    } else {
                        idx = paletteMid + (((int32_t)(pixel - centerS) * (PaletteSize - paletteMid)) / denomUpper);
                    }
                }
            }

            // Clamp
            if (idx < 0) idx = 0;
            if (idx >= (int)PaletteSize) idx = PaletteSize - 1;

            // Invert palette mapping so higher temperatures map to 'hot' colors
            int16_t invIdx = (int16_t)(PaletteSize - 1 - idx);
            if (invIdx < 0) invIdx = 0;
            if (invIdx >= (int)PaletteSize) invIdx = PaletteSize - 1;

            uint16_t color = RGB565(pPalette[invIdx].r, pPalette[invIdx].g, pPalette[invIdx].b);
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

typedef enum {
    SECTION_TITLE = 0,
    SECTION_IMAGE,
    SECTION_DATA,
    SECTION_COUNT
} focus_section_t;

static focus_section_t currentFocus = SECTION_IMAGE;

// 标题区域的子选择（左、中、右三项）
typedef enum {
    TITLE_SUB_LEFT = 0,   // 左侧：BOM
    TITLE_SUB_CENTER,     // 中间：BOM_FRUIT
    TITLE_SUB_RIGHT,      // 右侧：BOM
    TITLE_SUB_COUNT
} title_sub_selection_t;

static title_sub_selection_t currentTitleSubSelection = TITLE_SUB_LEFT;

// 底部数据区域的子选择（左、中、右三项）
typedef enum {
    DATA_SUB_LEFT = 0,   // 左侧：Max/Min/Ctr温度
    DATA_SUB_CENTER,     // 中间：边框区域
    DATA_SUB_RIGHT,      // 右侧：Atr/Set FPS
    DATA_SUB_COUNT
} data_sub_selection_t;

static data_sub_selection_t currentDataSubSelection = DATA_SUB_LEFT;

// 图像区域十字线显示状态
static bool showImageCrosshair = false;

// 温度单位：true为华氏度，false为摄氏度
static bool useFahrenheit = false;

static void DrawSectionFocus(focus_section_t focus,
                             uint16_t top_bar_h,
                             uint16_t bottom_bar_h,
                             uint16_t img_y_start,
                             uint16_t img_height)
{
    const int16_t xPos = 8;
    int16_t yPos[SECTION_COUNT];
    yPos[SECTION_TITLE] = top_bar_h / 2;
    yPos[SECTION_IMAGE] = img_y_start + (img_height / 2);
    yPos[SECTION_DATA] = dispcolor_getHeight() - (bottom_bar_h / 2);

    // for (int i = 0; i < SECTION_COUNT; i++) {
    //     dispcolor_DrawCircleFilled(xPos, yPos[i], 4, BLACK);
    // }

    dispcolor_DrawCircleFilled(xPos, yPos[focus], 4, RGB565(255, 255, 0));
}

// 渲染任务 - 使用render_task的图像优化方法
void render_task(void* arg)
{
    TickType_t perf_start, perf_mlx_read, perf_compute, perf_render, perf_lcd;
    
    printf("Render task started for thermal imaging display\n");
    
    // 分配图像缓冲区 - 使用屏幕尺寸和上下栏高度保持一致
    const int pixelCount = THERMALIMAGE_RESOLUTION_WIDTH * THERMALIMAGE_RESOLUTION_HEIGHT;
    const uint16_t top_bar_h = 20;   // 顶部标题栏高度（pixels）
    const uint16_t bottom_bar_h = 55; // 底部信息栏高度（pixels）
    const uint16_t hq_img_width = dispcolor_getWidth() - 20;   // 热成像显示宽度（与屏幕宽度一致）
    const uint16_t hq_img_height = dispcolor_getHeight() - top_bar_h - bottom_bar_h;  // 可用显示高度
    
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
        // 等待MLX90640数据更新或按键（保留按键事件以便简单菜单）
        EventBits_t uxBitsToWaitFor = RENDER_MLX90640_NO0 | RENDER_MLX90640_NO1 |
                         RENDER_ShortPress_Up | RENDER_Hold_Up |
                         RENDER_ShortPress_Center | RENDER_Hold_Center |
                         RENDER_ShortPress_Down | RENDER_Hold_Down;
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

            // 计算最大温度、最小温度、中间温度 - 参考render_task.c
            CalcTempFromMLX90640(frame);

            // 使用自动刻度模式或手动模式
            float minTemp, maxTemp;
            if (settingsParms.AutoScaleMode) {
                minTemp = frame->minT;
                maxTemp = frame->maxT;
            } else {
                minTemp = settingsParms.minTempNew;
                maxTemp = settingsParms.maxTempNew;
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

            // 清除文字显示区域，避免重叠（使用屏幕尺寸而不是魔法数字）
            dispcolor_FillRect(0, 0, dispcolor_getWidth(), top_bar_h, BLACK);      // 顶部标题区域
            dispcolor_FillRect(0, dispcolor_getHeight() - bottom_bar_h, dispcolor_getWidth(), bottom_bar_h, BLACK);    // 底部信息区域
            dispcolor_FillRect(0, 20, 10, 165, BLACK);      // 热成像左侧区域


            // 显示标题（根据字体高度垂直居中）
            uint8_t* pTitleFont = font_GetFontStruct(FONTID_16F, 'A');
            uint8_t titleFontH = font_GetCharHeight(pTitleFont);
            int16_t title_y = (int16_t)((top_bar_h - titleFontH) / 2);
            if (title_y < 0) title_y = 0;
            // dispcolor_DrawString(10, title_y, FONTID_16F, (uint8_t*)"Thermal Camera", WHITE);
            int16_t title_x = dispcolor_getStrWidth(FONTID_16F, "BOM_FRUIT");
            int16_t title_x2 = dispcolor_getStrWidth(FONTID_16F, "BOM");

            title_x = (dispcolor_getWidth() - title_x) / 2;
            
            // 确定各标题项的颜色（如果焦点在标题区域，高亮当前选中的子项）
            uint16_t leftTitleColor = WHITE;
            uint16_t centerTitleColor = WHITE;
            uint16_t rightTitleColor = WHITE;
            
            if (currentFocus == SECTION_TITLE) {
                if (currentTitleSubSelection == TITLE_SUB_LEFT) {
                    leftTitleColor = RGB565(0, 200, 255); // 蓝色高亮
                } else if (currentTitleSubSelection == TITLE_SUB_CENTER) {
                    centerTitleColor = RGB565(0, 200, 255); // 蓝色高亮
                } else if (currentTitleSubSelection == TITLE_SUB_RIGHT) {
                    rightTitleColor = RGB565(0, 200, 255); // 蓝色高亮
                }
            }
            
            // 右侧显示温度单位指示
            const char* tempUnitText = useFahrenheit ? FAHRENHEIT_SYMBOL : CELSIUS_SYMBOL;
            int16_t tempUnitWidth = dispcolor_getStrWidth(FONTID_16F, tempUnitText);
            
            dispcolor_DrawString(title_x, title_y, FONTID_16F, (uint8_t*)"BOM_FRUIT", centerTitleColor);
            dispcolor_DrawString(10, title_y, FONTID_16F, (uint8_t*)"BOM", leftTitleColor);
            dispcolor_DrawString(230-tempUnitWidth, title_y, FONTID_16F, (uint8_t*)tempUnitText, rightTitleColor);


            // 使用高斯模糊+双线性插值优化 - 参考render_task.c的HQ3X_2X模式
            // 计算热成像显示区域（留出顶部和底部栏）
            const uint16_t img_y_start = top_bar_h;
            const uint16_t img_height = hq_img_height;  // 可用像素高度
            const uint16_t img_width = hq_img_width;
            const uint16_t img_x_start = (dispcolor_getWidth() - img_width) / 2;  // 居中显示（通常为0）
            
            // 步骤1: 高斯模糊2倍放大
            idwGauss(TermoImage16, THERMALIMAGE_RESOLUTION_WIDTH, THERMALIMAGE_RESOLUTION_HEIGHT, 2, gaussBuff);
            
            // 步骤2: 双线性插值到指定区域
            idwBilinear(gaussBuff, THERMALIMAGE_RESOLUTION_WIDTH * 2, THERMALIMAGE_RESOLUTION_HEIGHT * 2, 
                       TermoHqImage16, img_width, img_height, 10 / 2);
            
            // 步骤3: 绘制高质量图像到指定区域
            DrawHQImage(TermoHqImage16, pPaletteImage, paletteSteps, img_x_start, img_y_start, 
                       img_width, img_height, minTemp, maxTemp);
            
            // 热图上的最大/最小标记和中心温度 - 参考render_task.c
            if (settingsParms.TempMarkers) {
                // 在屏幕中央显示温度（使用当前选择的温度单位）
                DrawCenterTemp(img_x_start, img_y_start, img_width, img_height, frame->CenterTemp, useFahrenheit);
                
                // 标记最大最小点
                // DrawMarkersHQ(frame, img_x_start, img_y_start, img_width, img_height);
            }
            
            // 绘制十字线（如果启用）
            if (showImageCrosshair) {
                // 计算图像中心位置
                uint16_t center_x = img_x_start + (img_width / 2);
                uint16_t center_y = img_y_start + (img_height / 2);
                
                // 绘制水平十字线（贯穿整个图像宽度）
                dispcolor_DrawLine(img_x_start, center_y, img_x_start + img_width - 1, center_y, WHITE);
                
                // 绘制垂直十字线（贯穿整个图像高度）
                dispcolor_DrawLine(center_x, img_y_start, center_x, img_y_start + img_height - 1, WHITE);
            }
            
            perf_render = xTaskGetTickCount();
            
            if (settingsParms.RealTimeAnalysis) {
                // 显示温度范围信息和帧率
                uint16_t leftColor = WHITE;
                uint16_t centerColor = WHITE;
                uint16_t rightColor = WHITE;
                
                // 如果焦点在底部数据区域，高亮当前选中的子项
                if (currentFocus == SECTION_DATA) {
                    if (currentDataSubSelection == DATA_SUB_LEFT) {
                        leftColor = RGB565(0, 200, 255); // 蓝色高亮
                    } else if (currentDataSubSelection == DATA_SUB_CENTER) {
                        centerColor = RGB565(0, 200, 255); // 蓝色高亮
                    } else if (currentDataSubSelection == DATA_SUB_RIGHT) {
                        rightColor = RGB565(0, 200, 255); // 蓝色高亮
                    }
                }
                
                dispcolor_printf(10, 190, FONTID_6X8M, leftColor, "Max:%.1f%s", frame->maxT, CELSIUS_SYMBOL);
                dispcolor_printf(10, 210, FONTID_6X8M, leftColor, "Min:%.1f%s", frame->minT, CELSIUS_SYMBOL);
                dispcolor_printf(10, 230, FONTID_6X8M, leftColor, "Ctr:%.1f%s", frame->CenterTemp, CELSIUS_SYMBOL);

                dispcolor_printf(170, 190, FONTID_6X8M, rightColor, "Atr:%.1fFPS", actual_fps);
                dispcolor_printf(170, 230, FONTID_6X8M, rightColor, "Set:%.1fFPS", FPS_RATES[settingsParms.MLX90640FPS]);

                dispcolor_DrawRectangle(75, 190, 165, 235, centerColor); // 边框
            } else {
                // 清除底部区域避免残留旧数据
                dispcolor_FillRect(0, dispcolor_getHeight() - bottom_bar_h, dispcolor_getWidth(), bottom_bar_h, BLACK);
            }

            DrawSectionFocus(currentFocus, top_bar_h, bottom_bar_h, img_y_start, img_height);
            
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
        
        // 如果等待返回的是按键事件（没有 MLX 帧位），处理按键
        if ((bits & (RENDER_ShortPress_Up | RENDER_Hold_Up | RENDER_ShortPress_Center | RENDER_Hold_Center | RENDER_ShortPress_Down | RENDER_Hold_Down)) != 0) {
            // 检查是否同时短按上下键且选中标题右侧项（温度单位转换键）
            bool isTempUnitToggle = (currentFocus == SECTION_TITLE && 
                                     currentTitleSubSelection == TITLE_SUB_RIGHT &&
                                     (bits & RENDER_ShortPress_Up) == RENDER_ShortPress_Up &&
                                     (bits & RENDER_ShortPress_Down) == RENDER_ShortPress_Down);
            
            // 处理同时短按上下键：切换温度单位
            if (isTempUnitToggle) {
                useFahrenheit = !useFahrenheit;
            }
            
            // 处理长按上下键：当焦点在标题区域时，用于左右切换子项
            if (currentFocus == SECTION_TITLE) {
                if ((bits & RENDER_Hold_Up) == RENDER_Hold_Up) {
                    // 长按上键：向左切换（或循环到最右）
                    currentTitleSubSelection = (currentTitleSubSelection == 0) ? (TITLE_SUB_COUNT - 1) : (currentTitleSubSelection - 1);
                }
                if ((bits & RENDER_Hold_Down) == RENDER_Hold_Down) {
                    // 长按下键：向右切换
                    currentTitleSubSelection = (currentTitleSubSelection + 1) % TITLE_SUB_COUNT;
                }
            }
            
            // 处理长按上下键：当焦点在图像区域时，用于显示/隐藏十字线
            if (currentFocus == SECTION_IMAGE) {
                if ((bits & RENDER_Hold_Up) == RENDER_Hold_Up) {
                    // 长按上键：去除十字线
                    showImageCrosshair = false;
                }
                if ((bits & RENDER_Hold_Down) == RENDER_Hold_Down) {
                    // 长按下键：显示十字线
                    showImageCrosshair = true;
                }
            }
            
            // 处理长按上下键：当焦点在底部数据区域时，用于左右切换子项
            if (currentFocus == SECTION_DATA) {
                if ((bits & RENDER_Hold_Up) == RENDER_Hold_Up) {
                    // 长按上键：向左切换（或循环到最右）
                    currentDataSubSelection = (currentDataSubSelection == 0) ? (DATA_SUB_COUNT - 1) : (currentDataSubSelection - 1);
                }
                if ((bits & RENDER_Hold_Down) == RENDER_Hold_Down) {
                    // 长按下键：向右切换
                    currentDataSubSelection = (currentDataSubSelection + 1) % DATA_SUB_COUNT;
                }
            }
            
            // 处理单独短按上下键：在三个主要区域之间切换（排除同时短按的情况）
            if (!isTempUnitToggle && (bits & RENDER_ShortPress_Up) == RENDER_ShortPress_Up) {
                currentFocus = (currentFocus == 0) ? (SECTION_COUNT - 1) : (currentFocus - 1);
                // 切换到其他区域时，重置子选择
                if (currentFocus != SECTION_TITLE) {
                    currentTitleSubSelection = TITLE_SUB_LEFT;
                }
                if (currentFocus != SECTION_DATA) {
                    currentDataSubSelection = DATA_SUB_LEFT;
                }
                // 切换到其他区域时，不自动隐藏十字线（保持状态）
            }
            if (!isTempUnitToggle && (bits & RENDER_ShortPress_Down) == RENDER_ShortPress_Down) {
                currentFocus = (currentFocus + 1) % SECTION_COUNT;
                // 切换到其他区域时，重置子选择
                if (currentFocus != SECTION_TITLE) {
                    currentTitleSubSelection = TITLE_SUB_LEFT;
                }
                if (currentFocus != SECTION_DATA) {
                    currentDataSubSelection = DATA_SUB_LEFT;
                }
                // 切换到其他区域时，不自动隐藏十字线（保持状态）
            }
            
            if ((bits & RENDER_ShortPress_Center) == RENDER_ShortPress_Center) {
                // 短按 Center：进入简易菜单
                menu_run_simple();
                settings_write_all();
                dispcolor_ClearScreen();
            }
            if ((bits & RENDER_Hold_Center) == RENDER_Hold_Center) {
                // Center 长按：进入简易菜单
                // menu_run_simple();
                // settings_write_all();
                // dispcolor_ClearScreen();
            }
        }

        // 不需要额外延迟，xEventGroupWaitBits已经会等待新数据
        // vTaskDelay(50 / portTICK_PERIOD_MS);  // 移除：这个延迟导致FPS被限制
    }
}