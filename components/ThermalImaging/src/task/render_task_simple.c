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
#include <stdio.h>
#include "simple_menu.h"
#include "settings.h"


#define TEMP_SCALE 10  // 温度放大倍数，与render_task.c一致
//test on DELL


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

// 子项选择模式：true表示当前在子项中左右选择，false表示在焦点区域间切换
static bool subItemMode = false;

// 调色板选择模式：true表示当前正在用编码器调整调色板
static bool paletteSelectMode = false;

// 温度单位选择模式：true表示当前正在用编码器切换温度单位
static bool tempUnitSelectMode = false;

// 通道选择模式：true 表示正在选择 X/Y 通道，需要右拨确认退出
static bool channelSelectMode = false;

// 可移动十字线模式
static bool crosshairMode = false;         // true 表示正在移动十字线
static bool crosshairAxisIsY = false;     // 在 crosshairMode 下，false=移动 X (列), true=移动 Y (行)
// 十字线坐标（列/行）。初始化为中心，但不会在退出/重新进入模式时自动复位，
// 用户调节后保持最后一次手动设置的位置，渲染循环直接使用这个位置。
static int cross_x = (THERMALIMAGE_RESOLUTION_WIDTH >> 1);
static int cross_y = (THERMALIMAGE_RESOLUTION_HEIGHT >> 1);

// 图像区域十字线显示状态
static bool showImageCrosshair = false;

// 温度单位：true为华氏度，false为摄氏度
static bool useFahrenheit = false;
// 绘图通道：false -> X轴(中心行)，true -> Y轴(中心列)
static bool plotChannelY = false;
// 请求立即重绘（即使没有新MLX帧），由输入处理器设置，渲染循环检测并执行一次绘制
static bool forceRender = false;
// 临时覆盖消息（短时提示），由输入处理器设置，渲染循环负责绘制并超时清除
static bool overlay_active = false;
static char overlay_line1[64] = {0};
static char overlay_line2[64] = {0};
static TickType_t overlay_expire_tick = 0;

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

    // 从持久化设置中恢复十字线位置（如果设置里有的话）
    cross_x = settingsParms.CrossX;
    cross_y = settingsParms.CrossY;

    // 主循环
    while (1) {
        // 等待MLX90640数据更新或输入事件
        // Wheel: 左拨=返回, 右拨/按下=确认进入菜单
        // SIQ02: 左转=向上, 右转=向下, 按下=确认
        EventBits_t uxBitsToWaitFor = RENDER_MLX90640_NO0 | RENDER_MLX90640_NO1 |
                         RENDER_Wheel_Back | RENDER_Wheel_Confirm | RENDER_Wheel_Press |
                         RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Encoder_Press;
        EventBits_t bits = xEventGroupWaitBits(pHandleEventGroup, uxBitsToWaitFor, pdTRUE, pdFALSE, portMAX_DELAY);

        bool hasMlx = ((bits & RENDER_MLX90640_NO0) == RENDER_MLX90640_NO0) || ((bits & RENDER_MLX90640_NO1) == RENDER_MLX90640_NO1);
        bool doRender = hasMlx || forceRender;
        if (doRender) {
            // 如果是强制重绘，清除请求标志（下次需要再次请求）
            forceRender = false;
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
            int16_t title_x = dispcolor_getStrWidth(FONTID_16F, "Palette 5");
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
            
            dispcolor_DrawString(title_x+10, title_y, FONTID_16F, (uint8_t*)"auto_save", centerTitleColor);
            // Show palette index same as simple menu (less verbose)
            dispcolor_printf(10, title_y, FONTID_16F, leftTitleColor, "Palette %d", settingsParms.ColorScale);
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
                // 计算十字线位置：始终使用 cross_x/cross_y（即使退出 crosshairMode 也保持手动调节的位置）
                int img_w = img_width;
                int img_h = img_height;
                // DrawHQImage uses (width - col - 1) + X horizontally inverted mapping
                int display_x = img_x_start + (img_w - 1) - (cross_x * (img_w - 1) / (THERMALIMAGE_RESOLUTION_WIDTH - 1));
                int display_y = img_y_start + (cross_y * (img_h - 1) / (THERMALIMAGE_RESOLUTION_HEIGHT - 1));

                // 绘制水平十字线（贯穿整个图像宽度）
                dispcolor_DrawLine(img_x_start, display_y, img_x_start + img_w - 1, display_y, WHITE);

                // 绘制垂直十字线（贯穿整个图像高度）
                dispcolor_DrawLine(display_x, img_y_start, display_x, img_y_start + img_h - 1, WHITE);
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
                
                // 左侧显示：overall-std / overall-max / overall-min / overall-avg
                {
                    int cntAll = THERMALIMAGE_RESOLUTION_WIDTH * THERMALIMAGE_RESOLUTION_HEIGHT;
                    double sumall = 0.0;
                    double sumsqall = 0.0;
                    double omin = 1e30;
                    double omax = -1e30;
                    int valid = 0;
                    for (int i = 0; i < cntAll; ++i) {
                        float v = frame->ThermoImage[i];
                        if (!isfinite(v)) continue;
                        valid++;
                        sumall += v;
                        sumsqall += (double)v * (double)v;
                        if (v < omin) omin = v;
                        if (v > omax) omax = v;
                    }
                    double oavg = 0.0;
                    double ostd = 0.0;
                    if (valid > 0) {
                        oavg = sumall / valid;
                        double var = sumsqall / valid - oavg * oavg;
                        if (var < 0) var = 0;
                        ostd = sqrt(var);
                    }

                    // 如果使用华氏度，转换显示值（注意：标准差按比例放大，不加偏移）
                    const char* dataUnit = useFahrenheit ? FAHRENHEIT_SYMBOL : CELSIUS_SYMBOL;
                    double disp_ostd = ostd;
                    double disp_omax = omax;
                    double disp_omin = omin;
                    double disp_oavg = oavg;
                    if (useFahrenheit) {
                        disp_ostd = ostd * 9.0 / 5.0;
                        disp_omax = omax * 9.0 / 5.0 + 32.0;
                        disp_omin = omin * 9.0 / 5.0 + 32.0;
                        disp_oavg = oavg * 9.0 / 5.0 + 32.0;
                    }

                    dispcolor_printf(10, 190, FONTID_6X8M, leftColor, "std:%.2f%s", (float)disp_ostd, dataUnit);
                    dispcolor_printf(10, 200, FONTID_6X8M, leftColor, "max:%.1f%s", (float)disp_omax, dataUnit);
                    dispcolor_printf(10, 210, FONTID_6X8M, leftColor, "min:%.1f%s", (float)disp_omin, dataUnit);
                    dispcolor_printf(10, 220, FONTID_6X8M, leftColor, "avg:%.2f%s", (float)disp_oavg, dataUnit);
                }

                // 在中间边框内绘制十字线的温度折线图（可在X轴中心行或Y轴中心列之间切换）
                dispcolor_DrawRectangle(75, 190, 165, 235, centerColor); // 边框
                {
                    const int16_t box_x1 = 75, box_y1 = 190, box_x2 = 165, box_y2 = 235;
                    const int16_t plot_margin = 2;
                    const int16_t plot_x = box_x1 + plot_margin;
                    const int16_t plot_y = box_y1 + plot_margin;
                    const int16_t plot_w = (box_x2 - box_x1) - 2 * plot_margin;
                    const int16_t plot_h = (box_y2 - box_y1) - 2 * plot_margin;

                    // 使用当前显示范围作为 Y 轴范围
                    float plot_min = minTemp;
                    float plot_max = maxTemp;
                    float plot_range = plot_max - plot_min;
                    if (plot_range < 0.1f) plot_range = 1.0f;

                    int16_t prev_px = -1, prev_py = -1;
                    const int center_row = THERMALIMAGE_RESOLUTION_HEIGHT / 2;
                    const int center_col = THERMALIMAGE_RESOLUTION_WIDTH / 2;

                    // 折线图通道严格由底部数据区域的通道选择决定（plotChannelY）
                    // 位置（哪一行/列）始终使用十字线位置 cross_x/cross_y（即使退出 crosshairMode 也保持手动调节的位置）
                    bool useYPlot = plotChannelY;
                    int target_row = cross_y;
                    int target_col = cross_x;

                    if (!useYPlot) {
                        // X 通道：使用某一行（水平）
                        for (int col = 0; col < THERMALIMAGE_RESOLUTION_WIDTH; col++) {
                            float t = frame->ThermoImage[target_row * THERMALIMAGE_RESOLUTION_WIDTH + col];
                            if (!isfinite(t)) continue;

                            int16_t px = plot_x + (col * plot_w) / (THERMALIMAGE_RESOLUTION_WIDTH - 1);
                            float norm = (t - plot_min) / plot_range;
                            if (norm < 0.0f) norm = 0.0f;
                            if (norm > 1.0f) norm = 1.0f;
                            int16_t py = plot_y + plot_h - 1 - (int16_t)(norm * (plot_h - 1));

                            if (prev_px >= 0) {
                                dispcolor_DrawLine(prev_px, prev_py, px, py, RGB565(0, 255, 128));
                            }
                            prev_px = px;
                            prev_py = py;
                        }
                    } else {
                        // Y 通道：使用某一列（垂直），沿着行方向绘制到水平绘图区域
                        for (int row = 0; row < THERMALIMAGE_RESOLUTION_HEIGHT; row++) {
                            float t = frame->ThermoImage[row * THERMALIMAGE_RESOLUTION_WIDTH + target_col];
                            if (!isfinite(t)) continue;

                            int16_t px = plot_x + (row * plot_w) / (THERMALIMAGE_RESOLUTION_HEIGHT - 1);
                            float norm = (t - plot_min) / plot_range;
                            if (norm < 0.0f) norm = 0.0f;
                            if (norm > 1.0f) norm = 1.0f;
                            int16_t py = plot_y + plot_h - 1 - (int16_t)(norm * (plot_h - 1));

                            if (prev_px >= 0) {
                                dispcolor_DrawLine(prev_px, prev_py, px, py, RGB565(0, 255, 128));
                            }
                            prev_px = px;
                            prev_py = py;
                        }
                    }

                    // 在右下角显示 FPS（小字）
                    char fpsStr[16];
                    sprintf(fpsStr, "%.0f", actual_fps);
                    dispcolor_printf(box_x2 - 28, box_y2 - 10, FONTID_6X8M, centerColor, "%s", fpsStr);
                }

                // 右侧显示：通道选择（X/Y）以及上量程和下量程
                dispcolor_printf(170, 190, FONTID_6X8M, rightColor, "Chan:%c", (plotChannelY ? 'Y' : 'X'));
                dispcolor_printf(170, 210, FONTID_6X8M, rightColor, "Hi:%.1f", maxTemp);
                dispcolor_printf(170, 230, FONTID_6X8M, rightColor, "Lo:%.1f", minTemp);
            } else {
                // 清除底部区域避免残留旧数据
                dispcolor_FillRect(0, dispcolor_getHeight() - bottom_bar_h, dispcolor_getWidth(), bottom_bar_h, BLACK);
            }

            DrawSectionFocus(currentFocus, top_bar_h, bottom_bar_h, img_y_start, img_height);
            
            // 在最终提交到LCD之前，如果有overlay提示则绘制
            if (overlay_active) {
                TickType_t now = xTaskGetTickCount();
                if (now < overlay_expire_tick) {
                    const int16_t msg_x = 140;
                    const int16_t msg_y = 184;
                    const int16_t msg_w = dispcolor_getWidth() - msg_x - 8;
                    const int16_t msg_h = 32;
                    dispcolor_FillRect(msg_x, msg_y, msg_w, msg_h, BLACK);
                    if (overlay_line1[0]) dispcolor_printf(msg_x, msg_y, FONTID_6X8M, RGB565(255,128,0), "%s", overlay_line1);
                    if (overlay_line2[0]) dispcolor_printf(msg_x, msg_y + 14, FONTID_6X8M, RGB565(200,200,200), "%s", overlay_line2);
                } else {
                    overlay_active = false;
                    overlay_line1[0] = 0; overlay_line2[0] = 0;
                }
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
        
        // 处理 Wheel 和 SIQ02 编码器事件
        // Wheel: 右拨=进入子项选择模式(或进入菜单), 左拨=退出子项模式
        // SIQ02: 子项模式下左右切换子项, 非子项模式下上下切换焦点区域
        // 特殊: 在palette子项时右拨进入调色板选择模式，编码器调整调色板
        
        // Wheel 右拨：进入子项选择模式，或从子项模式进入菜单/调色板选择/温度单位选择
        if (bits & RENDER_Wheel_Confirm) {
            // 如果焦点在图像区域且不在子项模式，右拨进入可移动十字线模式
            if (currentFocus == SECTION_IMAGE && !subItemMode && !crosshairMode) {
                crosshairMode = true;
                crosshairAxisIsY = false; // 默认开始调整 X
                showImageCrosshair = true;
                // 设置覆盖提示，由渲染循环负责绘制并自动超时
                snprintf(overlay_line1, sizeof(overlay_line1), "Crosshair mode: %s", (crosshairAxisIsY ? "Y" : "X"));
                snprintf(overlay_line2, sizeof(overlay_line2), "Enc:move  Right:axis  Left:exit");
                overlay_active = true;
                overlay_expire_tick = xTaskGetTickCount() + pdMS_TO_TICKS(800);
                forceRender = true;
                continue;
            // 如果当前已经处于十字线模式，则右拨切换调整轴（X/Y）
            } else if (currentFocus == SECTION_IMAGE && crosshairMode) {
                crosshairAxisIsY = !crosshairAxisIsY;
                // 提示新的轴方向，由渲染循环绘制
                snprintf(overlay_line1, sizeof(overlay_line1), "Crosshair axis: %s", (crosshairAxisIsY ? "Y" : "X"));
                overlay_line2[0] = '\0';
                overlay_active = true;
                overlay_expire_tick = xTaskGetTickCount() + pdMS_TO_TICKS(400);
                forceRender = true;
                continue;
            }
            if (paletteSelectMode) {
                // 在调色板选择模式，右拨确认并退出
                settings_write_all();
                paletteSelectMode = false;
            } else if (channelSelectMode) {
                // 在通道选择模式，右拨确认并退出（不持久化，仅退出选择）
                channelSelectMode = false;
            } else if (tempUnitSelectMode) {
                // 在温度单位选择模式，右拨确认并退出
                tempUnitSelectMode = false;
            } else if (!subItemMode) {
                // 进入子项选择模式
                subItemMode = true;
            } else {
                // 已在子项模式
                if (currentFocus == SECTION_TITLE && currentTitleSubSelection == TITLE_SUB_LEFT) {
                    // 在palette子项，进入调色板选择模式
                    paletteSelectMode = true;
                } else if (currentFocus == SECTION_TITLE && currentTitleSubSelection == TITLE_SUB_RIGHT) {
                    // 在温度单位子项，进入温度单位选择模式
                    tempUnitSelectMode = true;
                } else if (currentFocus == SECTION_DATA && currentDataSubSelection == DATA_SUB_RIGHT) {
                    // 在底部右侧子项，进入通道选择模式，需要右拨确认退出
                    channelSelectMode = true;
                } else {
                    // 其他子项，进入菜单
                    // menu_run_simple();
                    // settings_write_all();
                    // dispcolor_ClearScreen();
                    // subItemMode = false;
                }
            }
        }
        
        // Wheel 按下：直接进入菜单
        // if (bits & RENDER_Wheel_Press) {
        //     if (paletteSelectMode) {
        //         settings_write_all();
        //         paletteSelectMode = false;
        //     } else if (tempUnitSelectMode) {
        //         tempUnitSelectMode = false;
        //     } else {
        //         menu_run_simple();
        //         settings_write_all();
        //         dispcolor_ClearScreen();
        //         subItemMode = false;
        //     }
        // }
        
        // Wheel 左拨：退出调色板选择/温度单位选择/子项模式，返回焦点区域切换
        if (bits & RENDER_Wheel_Back) {
            if (paletteSelectMode) {
                // 退出调色板选择模式（不保存）
                paletteSelectMode = false;
            } else if (tempUnitSelectMode) {
                // 退出温度单位选择模式
                tempUnitSelectMode = false;
            } else if (channelSelectMode) {
                // 退出通道选择模式（取消）
                channelSelectMode = false;
            } else if (crosshairMode) {
                // 退出十字线移动模式
                crosshairMode = false;
                // 设置短时提示（由渲染任务绘制）
                // snprintf(overlay_line1, sizeof(overlay_line1), "Crosshair exit");
                overlay_line2[0] = '\0';
                overlay_active = true;
                overlay_expire_tick = xTaskGetTickCount() + pdMS_TO_TICKS(400);
                forceRender = true;
                // 保存十字线位置到持久化设置
                settingsParms.CrossX = (uint8_t)cross_x;
                settingsParms.CrossY = (uint8_t)cross_y;
                settings_write_all();
            } else if (subItemMode) {
                subItemMode = false;
            } else {
                // 已在焦点模式，可以重置到默认
                // currentFocus = SECTION_IMAGE;
                menu_run_simple();
                settings_write_all();
                dispcolor_ClearScreen();
                subItemMode = false;
            }
        }
        
        // SIQ02 编码器左转
        if (bits & RENDER_Encoder_Up) {
            if (paletteSelectMode) {
                // 调色板选择模式：切换到上一个调色板
                if (settingsParms.ColorScale == 0) {
                    settingsParms.ColorScale = COLOR_MAX - 1;
                } else {
                    settingsParms.ColorScale--;
                }
            } else if (tempUnitSelectMode) {
                // 温度单位选择模式：切换温度单位
                useFahrenheit = !useFahrenheit;
            } else if (channelSelectMode) {
                // 通道选择模式：旋转编码器切换通道
                plotChannelY = !plotChannelY;
            } else if (crosshairMode) {
                // 十字线模式：编码器移动十字线位置（向上=增加索引）
                if (crosshairAxisIsY) {
                    if (cross_y < (THERMALIMAGE_RESOLUTION_HEIGHT - 1)) cross_y++;
                } else {
                    if (cross_x < (THERMALIMAGE_RESOLUTION_WIDTH - 1)) cross_x++;
                }
                // 实时绘制下方折线和十字线提示（如果有最新帧数据）
                if (pMlxData != NULL) {
                    // 不在此处直接绘制，由渲染循环统一绘制
                    forceRender = true;
                }
            } else if (subItemMode) {
                // 子项模式：编码器切换子项（不直接切换通道，需先进入 channelSelectMode）
                if (currentFocus == SECTION_TITLE) {
                    currentTitleSubSelection = (currentTitleSubSelection == 0) ? (TITLE_SUB_COUNT - 1) : (currentTitleSubSelection - 1);
                } else if (currentFocus == SECTION_DATA) {
                    currentDataSubSelection = (currentDataSubSelection == 0) ? (DATA_SUB_COUNT - 1) : (currentDataSubSelection - 1);
                }
                // 图像区域暂无子项
            } else {
                // 焦点模式：向上切换焦点区域
                currentFocus = (currentFocus == 0) ? (SECTION_COUNT - 1) : (currentFocus - 1);
            }
        }
        
        // SIQ02 编码器右转
        if (bits & RENDER_Encoder_Down) {
            if (paletteSelectMode) {
                // 调色板选择模式：切换到下一个调色板
                settingsParms.ColorScale = (settingsParms.ColorScale + 1) % COLOR_MAX;
            } else if (tempUnitSelectMode) {
                // 温度单位选择模式：切换温度单位
                useFahrenheit = !useFahrenheit;
            } else if (channelSelectMode) {
                // 通道选择模式：旋转编码器切换通道
                plotChannelY = !plotChannelY;
            } else if (crosshairMode) {
                // 十字线模式：编码器移动十字线位置（向下=减少索引）
                if (crosshairAxisIsY) {
                    if (cross_y > 0) cross_y--;
                } else {
                    if (cross_x > 0) cross_x--;
                }
                if (pMlxData != NULL) {
                    // 请求渲染循环更新显示（不在这里直接绘制）
                    forceRender = true;
                }
            } else if (subItemMode) {
                // 子项模式：编码器切换子项（不直接切换通道，需先进入 channelSelectMode）
                if (currentFocus == SECTION_TITLE) {
                    currentTitleSubSelection = (currentTitleSubSelection + 1) % TITLE_SUB_COUNT;
                } else if (currentFocus == SECTION_DATA) {
                    currentDataSubSelection = (currentDataSubSelection + 1) % DATA_SUB_COUNT;
                }
                // 图像区域暂无子项
            } else {
                // 焦点模式：向下切换焦点区域
                currentFocus = (currentFocus + 1) % SECTION_COUNT;
            }
        }
        
        // SIQ02 编码器按下：在图像区域切换十字线
        if (bits & RENDER_Encoder_Press) {
            if (currentFocus == SECTION_IMAGE) {
                showImageCrosshair = !showImageCrosshair;
            }
        }

        // 不需要额外延迟，xEventGroupWaitBits已经会等待新数据
        // vTaskDelay(50 / portTICK_PERIOD_MS);  // 移除：这个延迟导致FPS被限制
    }
}