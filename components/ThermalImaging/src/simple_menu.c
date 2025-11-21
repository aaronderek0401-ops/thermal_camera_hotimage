#include "simple_menu.h"
#include "thermalimaging_simple.h"
#include "dispcolor.h"
#include "CelsiusSymbol.h"
#include "render_task.h"
// #include "save.h"
#include <stdbool.h>

#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

// A very small menu that uses the existing event group button bits.
// Navigation: Up/Down to move, Center short press to toggle an option,
// Center long press (Hold) to exit the menu.

typedef enum {
    MENU_OPEN_CAMERA = 0,
    MENU_AUTO_SCALE,
    MENU_PALETTE_NEXT,
    MENU_REALTIME_ANALYSIS,
    MENU_ITEMS_COUNT
} menu_item_t;

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
            case MENU_PALETTE_NEXT:
                snprintf(labels[i], sizeof(labels[i]), "Palette: Next");
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
                dispcolor_DrawCircleFilled(dot_x, dot_y, 4, dotColor);
            } else {
                // 清除任何旧的点
                dispcolor_DrawRectangleFilled(dot_x - 5, dot_y - 5, dot_x + 5, dot_y + 5, BLACK);
            }

            // 文本
            dispcolor_printf(item_left + 8, y0 + 6, FONTID_6X8M, WHITE, "%s", labels[i]);
        }
        dispcolor_Update();

        // wait for button events
        EventBits_t uxBitsToWaitFor = RENDER_ShortPress_Up | RENDER_ShortPress_Down | RENDER_ShortPress_Center | RENDER_Hold_Center;
        EventBits_t bits = xEventGroupWaitBits(pHandleEventGroup, uxBitsToWaitFor, pdTRUE, pdFALSE, portMAX_DELAY);

        if ((bits & RENDER_ShortPress_Up) == RENDER_ShortPress_Up) {
            if (selected > 0) selected--; else selected = MENU_ITEMS_COUNT - 1;
        }
        if ((bits & RENDER_ShortPress_Down) == RENDER_ShortPress_Down) {
            if (selected < MENU_ITEMS_COUNT - 1) selected++; else selected = 0;
        }
        if ((bits & RENDER_ShortPress_Center) == RENDER_ShortPress_Center) {
            // perform a minimal action per item
            switch (selected) {
            case MENU_OPEN_CAMERA:
                exit = true; // 返回实时画面
                break;
            case MENU_AUTO_SCALE:
                settingsParms.AutoScaleMode = !settingsParms.AutoScaleMode;
                settings_write_all();
                break;
            case MENU_PALETTE_NEXT:
                settingsParms.ColorScale = (settingsParms.ColorScale + 1) % COLOR_MAX;
                // persist new palette selection; render_task_simple regenerates palette each frame
                settings_write_all();
                {
                    int16_t msg_x = 40;
                    int16_t msg_y = dispcolor_getHeight() - 28;
                    // clear area to avoid overlapping digits
                    dispcolor_DrawRectangleFilled(msg_x - 4, msg_y - 2, dispcolor_getWidth() - 20, msg_y + 12, BLACK);
                    dispcolor_printf(msg_x, msg_y, FONTID_6X8M, WHITE, "Palette %d", settingsParms.ColorScale);
                    dispcolor_Update();
                }
                vTaskDelay(300 / portTICK_PERIOD_MS);
                break;
            case MENU_REALTIME_ANALYSIS:
                settingsParms.RealTimeAnalysis = !settingsParms.RealTimeAnalysis;
                settings_write_all();
                break;
            }
        }
        if ((bits & RENDER_Hold_Center) == RENDER_Hold_Center) {
            exit = true; // long press center exits menu
        }
    }

    // clear full screen when exiting menu
    dispcolor_FillRect(0, 0, screen_w, screen_h, BLACK);
    dispcolor_Update();

    return 0;
}
