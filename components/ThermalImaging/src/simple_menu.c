#include "simple_menu.h"
#include "thermalimaging_simple.h"
#include "dispcolor.h"
#include "CelsiusSymbol.h"
#include "render_task.h"
#include "settings.h"
// #include "save.h"
#include <stdbool.h>

#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

// allow menu to apply new MLX rate immediately
extern int mlx90640_flushRate(void);
// runtime control for render quality in render_task_simple
extern bool lowQualityRender;

// A very small menu that uses wheel/encoder event bits.
// Navigation: Encoder Up/Down to move through items and adjust values,
// Wheel Confirm (right) to select/confirm, Wheel Back (left) to exit/cancel.

typedef enum {
    MENU_OPEN_CAMERA = 0,
    MENU_AUTO_SCALE,
    MENU_SET_MIN_TEMP,
    MENU_SET_PALETTE_CENTER,
    MENU_SET_MAX_TEMP,
    MENU_MLX_FPS,
    MENU_REALTIME_ANALYSIS,
    MENU_ITEMS_COUNT
} menu_item_t;

// Human-readable MLX90640 refresh rate strings (indices 0..7 -> 0.5..64 Hz)
static const char* MLX_FPS_STR[] = { "0.5", "1", "2", "4", "8", "16", "32", "64" };

int menu_run_simple(void)
{
    if (pHandleEventGroup == NULL) return -1;

    // draw a full-screen menu background
    int16_t screen_w = dispcolor_getWidth();
    int16_t screen_h = dispcolor_getHeight();
    dispcolor_FillRect(0, 0, screen_w, screen_h, BLACK);
    // center title horizontally near the top
    int16_t title_w = dispcolor_getStrWidth(FONTID_16F, "Simple Menu");
    int16_t title_x = (screen_w - title_w) / 2;
    int16_t title_y = 12;
    if (title_x < 0) title_x = 0;
    dispcolor_printf(title_x, title_y, FONTID_16F, WHITE, "Simple Menu");

    int selected = 0;
    bool exit = false;

    while (!exit) {
        // render items (use wider full-width rows)
        int16_t item_left = 20;
        int16_t item_right = screen_w - 20;
        int16_t item_height = 28;
        int16_t item_top = 56;
        char labels[MENU_ITEMS_COUNT][40];

        for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
            switch (i) {
            case MENU_OPEN_CAMERA:
                snprintf(labels[i], sizeof(labels[i]), "Open Camera");
                break;
            case MENU_AUTO_SCALE:
                snprintf(labels[i], sizeof(labels[i]), "Auto Scale: %s", settingsParms.AutoScaleMode ? "On" : "Off");
                break;
            case MENU_SET_MIN_TEMP:
                snprintf(labels[i], sizeof(labels[i]), "Min Temp: %.1f%s", settingsParms.minTempNew, CELSIUS_SYMBOL);
                break;
            case MENU_SET_PALETTE_CENTER:
                snprintf(labels[i], sizeof(labels[i]), "Palette Center: %d%%", settingsParms.PaletteCenterPercent);
                break;
            case MENU_SET_MAX_TEMP:
                snprintf(labels[i], sizeof(labels[i]), "Max Temp: %.1f%s", settingsParms.maxTempNew, CELSIUS_SYMBOL);
                break;
            case MENU_MLX_FPS:
                {
                    // Only two options supported in this menu: 16Hz (index 5) and 32Hz (index 6)
                    bool is32 = (settingsParms.MLX90640FPS >= 6);
                    snprintf(labels[i], sizeof(labels[i]), "MLX FPS: %s Hz", is32 ? MLX_FPS_STR[6] : MLX_FPS_STR[5]);
                }
                break;
            case MENU_REALTIME_ANALYSIS:
                snprintf(labels[i], sizeof(labels[i]), "Real-time Data Analysis: %s", settingsParms.RealTimeAnalysis ? "On" : "Off");
                break;
            default:
                labels[i][0] = '\0';
                break;
            }
        }

        for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
            int16_t y0 = item_top + i * item_height;
            int16_t y1 = y0 + item_height - 6;
            // 背景统一为黑色
            dispcolor_DrawRectangleFilled(item_left, y0, item_right, y1, BLACK);

            int16_t dot_x = item_left - 12;
            int16_t dot_y = y0 + (y1 - y0) / 2;
            if (i == selected) {
                uint16_t dotColor = RGB565(255, 255, 0);
                dispcolor_DrawCircleFilled(dot_x, dot_y, 3, dotColor);
            } else {
                // 清除任何旧的点
                dispcolor_DrawRectangleFilled(dot_x - 5, dot_y - 5, dot_x + 5, dot_y + 5, BLACK);
            }

            // 文本
            dispcolor_printf(item_left + 8, y0 + 6, FONTID_6X8M, WHITE, "%s", labels[i]);
        }
        dispcolor_Update();

        // wait for wheel/encoder events
        EventBits_t uxBitsToWaitFor = RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back;
        EventBits_t bits = xEventGroupWaitBits(pHandleEventGroup, uxBitsToWaitFor, pdTRUE, pdFALSE, portMAX_DELAY);

        if ((bits & RENDER_Encoder_Up) == RENDER_Encoder_Up) {
            if (selected > 0) selected--; else selected = MENU_ITEMS_COUNT - 1;
        }
        if ((bits & RENDER_Encoder_Down) == RENDER_Encoder_Down) {
            if (selected < MENU_ITEMS_COUNT - 1) selected++; else selected = 0;
        }
        if ((bits & RENDER_Wheel_Confirm) == RENDER_Wheel_Confirm) {
            // perform a minimal action per item
            switch (selected) {
            case MENU_OPEN_CAMERA:
                exit = true; // 返回实时画面
                break;
            case MENU_AUTO_SCALE:
                settingsParms.AutoScaleMode = !settingsParms.AutoScaleMode;
                settings_write_all();
                break;
            case MENU_MLX_FPS: {
                // Interactive adjust loop for MLX90640 FPS index (0..7)
                // Only allow two choices: 16Hz (index 5) and 32Hz (index 6)
                uint8_t v = (settingsParms.MLX90640FPS >= 6) ? 6 : 5;
                bool done = false;
                // clear the menu list area to avoid interference and draw prompt area
                dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);
                // redraw title
                dispcolor_printf(title_x, title_y, FONTID_16F, WHITE, "Simple Menu");
                while (!done) {
                    dispcolor_FillRect(20, 100, dispcolor_getWidth() - 20, 160, BLACK);
                    dispcolor_printf(28, 108, FONTID_6X8M, WHITE, "Set MLX FPS: %s Hz", MLX_FPS_STR[v]);
                    dispcolor_printf(28, 128, FONTID_6X8M, WHITE, "Encoder:toggle, Wheel:save/cancel");
                    dispcolor_Update();

                    EventBits_t bits2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                    if (bits2 & (RENDER_Encoder_Up | RENDER_Encoder_Down)) {
                        // toggle between 16Hz (5) and 32Hz (6)
                        v = (v == 5) ? 6 : 5;
                    }

                    if (bits2 & RENDER_Wheel_Confirm) {
                        settingsParms.MLX90640FPS = v;
                        settings_write_all();
                        // apply immediately to sensor
                        mlx90640_flushRate();
                        // enable low quality render if 32Hz selected
                        lowQualityRender = (v == 6);
                        // confirmation
                        dispcolor_FillRect(20, 100, dispcolor_getWidth() - 20, 160, BLACK);
                        dispcolor_printf(28, 118, FONTID_6X8M, WHITE, "MLX FPS set: %s Hz", MLX_FPS_STR[v]);
                        if (lowQualityRender) dispcolor_printf(28, 136, FONTID_6X8M, WHITE, "LowQuality render: ON");
                        else dispcolor_printf(28, 136, FONTID_6X8M, WHITE, "LowQuality render: OFF");
                        dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);

                        dispcolor_Update();
                        vTaskDelay(600 / portTICK_PERIOD_MS);
                        done = true;
                    }
                    if (bits2 & RENDER_Wheel_Back) {
                        // cancel: clear adjustment area before returning to main menu
                        dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);
                        done = true;
                    }
                }
            } break;
            case MENU_SET_MIN_TEMP: {
                // Interactive adjust loop for min temp
                float v = settingsParms.minTempNew;
                bool done = false;
                // clear the menu list area to avoid interference and draw prompt area
                dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);
                // redraw title
                dispcolor_printf(title_x, title_y, FONTID_16F, WHITE, "Simple Menu");
                while (!done) {
                    dispcolor_FillRect(20, 100, dispcolor_getWidth() - 20, 160, BLACK);
                    dispcolor_printf(28, 108, FONTID_6X8M, WHITE, "Set Min Temp: %.1f%s", v, CELSIUS_SYMBOL);
                    dispcolor_printf(28, 128, FONTID_6X8M, WHITE, "Encoder:adjust, Wheel:save/cancel");
                    dispcolor_Update();

                    EventBits_t bits2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                    if (bits2 & RENDER_Encoder_Up) {
                        v += 1.0f;
                    }
                    if (bits2 & RENDER_Encoder_Down) {
                        v -= 1.0f;
                    }
                    // clamp so that max - min >= MIN_TEMPSCALE_DELTA
                    if (v > settingsParms.maxTempNew - MIN_TEMPSCALE_DELTA) v = settingsParms.maxTempNew - MIN_TEMPSCALE_DELTA;
                    if (v < -50.0f) v = -50.0f;

                    if (bits2 & RENDER_Wheel_Confirm) {
                        settingsParms.minTempNew = v;
                        settings_write_all();
                        // confirmation
                        dispcolor_FillRect(20, 100, dispcolor_getWidth() - 20, 160, BLACK);
                        dispcolor_printf(28, 118, FONTID_6X8M, WHITE, "Min temp set: %.1f%s", v, CELSIUS_SYMBOL);
                        dispcolor_Update();
                        vTaskDelay(600 / portTICK_PERIOD_MS);
                        done = true;
                    }
                    if (bits2 & RENDER_Wheel_Back) {
                        // cancel: clear adjustment area before returning to main menu
                        dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);
                        done = true;
                    }
                }
            } break;
            case MENU_SET_PALETTE_CENTER: {
                // Interactive adjust loop for palette center percent (0-100)
                uint8_t v = settingsParms.PaletteCenterPercent;
                bool done = false;
                // clear the menu list area to avoid interference and draw prompt area
                dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);
                // redraw title
                dispcolor_printf(title_x, title_y, FONTID_16F, WHITE, "Simple Menu");
                while (!done) {
                    dispcolor_FillRect(20, 100, dispcolor_getWidth() - 20, 160, BLACK);
                    dispcolor_printf(28, 108, FONTID_6X8M, WHITE, "Palette Center: %d%%", v);
                    dispcolor_printf(28, 128, FONTID_6X8M, WHITE, "Encoder:adjust, Wheel:save/cancel");
                    dispcolor_Update();

                    EventBits_t bits2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                    if (bits2 & RENDER_Encoder_Up) {
                        if (v < 100) v++;
                    }
                    if (bits2 & RENDER_Encoder_Down) {
                        if (v > 0) v--;
                    }

                    if (bits2 & RENDER_Wheel_Confirm) {
                        settingsParms.PaletteCenterPercent = v;
                        settings_write_all();
                        // confirmation
                        dispcolor_FillRect(20, 100, dispcolor_getWidth() - 20, 160, BLACK);
                        dispcolor_printf(28, 118, FONTID_6X8M, WHITE, "Palette center set: %d%%", v);
                        dispcolor_Update();
                        vTaskDelay(600 / portTICK_PERIOD_MS);
                        done = true;
                    }
                    if (bits2 & RENDER_Wheel_Back) {
                        // cancel: clear adjustment area before returning to main menu
                        dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);
                        done = true;
                    }
                }
            } break;
            case MENU_SET_MAX_TEMP: {
                // Interactive adjust loop for max temp
                float v = settingsParms.maxTempNew;
                bool done = false;
                // clear the menu list area to avoid interference and draw prompt area
                dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);
                // redraw title
                dispcolor_printf(title_x, title_y, FONTID_16F, WHITE, "Simple Menu");
                while (!done) {
                    dispcolor_FillRect(20, 100, dispcolor_getWidth() - 20, 160, BLACK);
                    dispcolor_printf(28, 108, FONTID_6X8M, WHITE, "Set Max Temp: %.1f%s", v, CELSIUS_SYMBOL);
                    dispcolor_printf(28, 128, FONTID_6X8M, WHITE, "Encoder:adjust, Wheel:save/cancel");
                    dispcolor_Update();

                    EventBits_t bits2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                    if (bits2 & RENDER_Encoder_Up) {
                        v += 1.0f;
                    }
                    if (bits2 & RENDER_Encoder_Down) {
                        v -= 1.0f;
                    }
                    // clamp so that max - min >= MIN_TEMPSCALE_DELTA
                    if (v < settingsParms.minTempNew + MIN_TEMPSCALE_DELTA) v = settingsParms.minTempNew + MIN_TEMPSCALE_DELTA;
                    if (v > 500.0f) v = 500.0f;

                    if (bits2 & RENDER_Wheel_Confirm) {
                        settingsParms.maxTempNew = v;
                        settings_write_all();
                        dispcolor_FillRect(20, 100, dispcolor_getWidth() - 20, 160, BLACK);
                        dispcolor_printf(28, 118, FONTID_6X8M, WHITE, "Max temp set: %.1f%s", v, CELSIUS_SYMBOL);
                        dispcolor_Update();
                        vTaskDelay(600 / portTICK_PERIOD_MS);
                        done = true;
                    }
                    if (bits2 & RENDER_Wheel_Back) {
                        // cancel: clear adjustment area before returning to main menu
                        dispcolor_FillRect(0, 40, screen_w, screen_h - 40, BLACK);
                        done = true;
                    }
                }
            } break;
            case MENU_REALTIME_ANALYSIS:
                settingsParms.RealTimeAnalysis = !settingsParms.RealTimeAnalysis;
                settings_write_all();
                break;
            }
        }
        if ((bits & RENDER_Wheel_Back) == RENDER_Wheel_Back) {
            exit = true; // wheel left exits menu
        }
    }

    // clear full screen when exiting menu
    dispcolor_FillRect(0, 0, screen_w, screen_h, BLACK);
    dispcolor_Update();

    return 0;
}
