#include "simple_menu.h"
#include "thermalimaging_simple.h"
#include "dispcolor.h"
#include "CelsiusSymbol.h"
#include "render_task.h"
#include "sleep.h"
#include "settings.h"
#include "save.h"
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

// --- GEEK STYLE THEME COLORS (RGB565) ---
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_CYAN    0x07FF  // Bright Cyan
#define C_DCYAN   0x0210  // Dark/Dim Cyan
#define C_GREY    0x8410  // Mid Grey

typedef enum {
    MENU_OPEN_CAMERA = 0,
    MENU_AUTO_SCALE,
    MENU_SET_MIN_TEMP,
    MENU_SET_PALETTE_CENTER,
    MENU_SET_MAX_TEMP,
    MENU_MLX_FPS,
    MENU_REALTIME_ANALYSIS,
    MENU_VIEW_SCREENSHOTS,
    MENU_DELETE_SCREENSHOTS,
    MENU_ITEMS_COUNT
} menu_item_t;

static const char* MLX_FPS_STR[] = { "0.5", "1", "2", "4", "8", "16", "32", "64" };

// Helper to clear a content area inside the menu frame
void draw_menu_clear_content(int16_t w, int16_t h) {
    // Keep header (0-20) and footer area, clear middle
    dispcolor_FillRect(0, 21, w, h - 21, C_BLACK);
}

// Helper to draw a "Geek" style value adjust box
void draw_adjust_overlay(const char* title, const char* value_str, const char* hint) {
    int16_t sw = dispcolor_getWidth();
    int16_t sh = dispcolor_getHeight();
    
    // Draw a bordered box in center
    int16_t bw = 200;
    int16_t bh = 80;
    int16_t bx = (sw - bw) / 2;
    int16_t by = (sh - bh) / 2;

    // Box Frame (Cyan Border, Black Fill)
    dispcolor_FillRect(bx - 1, by - 1, bw + 2, bh + 2, C_CYAN);
    dispcolor_FillRect(bx, by, bw, bh, C_BLACK);

    // Title Bar of Box
    dispcolor_FillRect(bx, by, bw, 18, C_CYAN);
    dispcolor_printf(bx + 4, by + 4, FONTID_6X8M, C_BLACK, "%s", title);

    // Value
    dispcolor_printf(bx + 20, by + 30, FONTID_16F, C_WHITE, "%s", value_str);

    // Hint
    dispcolor_printf(bx + 4, by + bh - 12, FONTID_6X8M, C_CYAN, "%s", hint);
}

int menu_run_simple(void)
{
    if (pHandleEventGroup == NULL) return -1;

    int16_t screen_w = dispcolor_getWidth();
    int16_t screen_h = dispcolor_getHeight();

    int selected = 0;
    bool exit = false;

    while (!exit) {
        // --- 1. Draw UI Background & Header ---
        dispcolor_FillRect(0, 0, screen_w, screen_h, C_BLACK);

        // Header Bar
        dispcolor_FillRect(0, 0, screen_w, 18, C_DCYAN); // Darker top bar
        dispcolor_FillRect(0, 18, screen_w, 1, C_CYAN);  // Separator line
        dispcolor_printf(4, 5, FONTID_6X8M, C_CYAN, "SYSTEM MENU");
        
        // Optional: Battery or Time could go here on the right

        // --- 2. Calculate Layout ---
        int16_t item_height = 18;  // Compact height
        int16_t list_top = 24;
        int16_t visible_area_h = screen_h - list_top - 14; // Leave room for footer
        int16_t max_visible = visible_area_h / item_height;
        
        static int scroll_offset = 0;
        if (selected < scroll_offset) scroll_offset = selected;
        else if (selected >= scroll_offset + max_visible) scroll_offset = selected - max_visible + 1;

        // --- 3. Render Items ---
        char label[64];
        char value[32];

        for (int i = scroll_offset; i < MENU_ITEMS_COUNT && i < scroll_offset + max_visible; i++) {
            int16_t y = list_top + (i - scroll_offset) * item_height;
            
            // Prepare Text
            value[0] = '\0'; // Default empty value
            switch (i) {
                case MENU_OPEN_CAMERA: strcpy(label, "Open Camera"); break;
                case MENU_AUTO_SCALE: 
                    strcpy(label, "Auto Scale"); 
                    snprintf(value, sizeof(value), "[%s]", settingsParms.AutoScaleMode ? "ON" : "OFF");
                    break;
                case MENU_SET_MIN_TEMP: 
                    strcpy(label, "Min Temp"); 
                    snprintf(value, sizeof(value), "%.1f%s", settingsParms.minTempNew, CELSIUS_SYMBOL);
                    break;
                case MENU_SET_PALETTE_CENTER: 
                    strcpy(label, "Palette Center"); 
                    snprintf(value, sizeof(value), "%d%%", settingsParms.PaletteCenterPercent);
                    break;
                case MENU_SET_MAX_TEMP: 
                    strcpy(label, "Max Temp"); 
                    snprintf(value, sizeof(value), "%.1f%s", settingsParms.maxTempNew, CELSIUS_SYMBOL);
                    break;
                case MENU_MLX_FPS: 
                    strcpy(label, "Sensor Rate"); 
                    snprintf(value, sizeof(value), "%s Hz", (settingsParms.MLX90640FPS >= 6) ? MLX_FPS_STR[6] : MLX_FPS_STR[5]);
                    break;
                case MENU_REALTIME_ANALYSIS: 
                    strcpy(label, "RT Analysis"); 
                    snprintf(value, sizeof(value), "[%s]", settingsParms.RealTimeAnalysis ? "ON" : "OFF");
                    break;
                case MENU_VIEW_SCREENSHOTS: strcpy(label, "Gallery"); break;
                case MENU_DELETE_SCREENSHOTS: strcpy(label, "Delete Files"); break;
                default: strcpy(label, "???"); break;
            }

            // Draw Item Row
            if (i == selected) {
                // Selected: Cyan Box, Black Text (Inverted)
                dispcolor_FillRect(2, y, screen_w - 14, item_height, C_CYAN);
                dispcolor_printf(6, y + 5, FONTID_6X8M, C_BLACK, "> %s", label);
                if(value[0]) dispcolor_printf(screen_w - 14 - (strlen(value)*6) - 4, y + 5, FONTID_6X8M, C_BLACK, "%s", value);
            } else {
                // Normal: Black Bg, Cyan Text
                dispcolor_printf(6, y + 5, FONTID_6X8M, C_DCYAN, "  %s", label); // Dim cyan
                if(value[0]) dispcolor_printf(screen_w - 14 - (strlen(value)*6) - 4, y + 5, FONTID_6X8M, C_DCYAN, "%s", value);
            }
        }

        // --- 4. Draw Scrollbar (Right Side) ---
        int16_t sb_x = screen_w - 6;
        int16_t sb_y = list_top;
        int16_t sb_h = max_visible * item_height;
        // Track
        dispcolor_FillRect(sb_x + 2, sb_y, 1, sb_h, C_DCYAN); 
        // Thumb
        int16_t thumb_h = (sb_h * max_visible) / MENU_ITEMS_COUNT;
        if (thumb_h < 4) thumb_h = 4;
        int16_t thumb_y = sb_y + (scroll_offset * (sb_h - thumb_h)) / (MENU_ITEMS_COUNT - max_visible > 0 ? MENU_ITEMS_COUNT - max_visible : 1);
        dispcolor_FillRect(sb_x, thumb_y, 5, thumb_h, C_CYAN);


        // --- 5. Footer / Status ---
        dispcolor_FillRect(0, screen_h - 12, screen_w, 1, C_DCYAN);
        dispcolor_printf(4, screen_h - 10, FONTID_6X8M, C_DCYAN, "UP/DN:NAV  WHEEL:SEL");

        dispcolor_Update();

        // --- Event Handling (Logic Unchanged) ---
        EventBits_t bits = xEventGroupWaitBits(pHandleEventGroup, 
            RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, 
            pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & RENDER_Encoder_Up) {
            if (selected > 0) selected--; else selected = MENU_ITEMS_COUNT - 1;
        }
        if (bits & RENDER_Encoder_Down) {
            if (selected < MENU_ITEMS_COUNT - 1) selected++; else selected = 0;
        }
        if (bits & RENDER_Wheel_Back) {
            system_enter_deep_sleep();
            exit = true;
            continue;
        }

        if (bits & RENDER_Wheel_Confirm) {
            switch (selected) {
                case MENU_OPEN_CAMERA:
                    exit = true; 
                    break;
                case MENU_AUTO_SCALE:
                    settingsParms.AutoScaleMode = !settingsParms.AutoScaleMode;
                    settings_write_all();
                    break;
                case MENU_MLX_FPS: {
                    uint8_t v = (settingsParms.MLX90640FPS >= 6) ? 6 : 5;
                    bool done = false;
                    while (!done) {
                        draw_adjust_overlay("SENSOR RATE", MLX_FPS_STR[v], "UP/DN:Chg OK:Save");
                        dispcolor_Update();

                        EventBits_t b2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                        if (b2 & (RENDER_Encoder_Up | RENDER_Encoder_Down)) v = (v == 5) ? 6 : 5;
                        if (b2 & RENDER_Wheel_Confirm) {
                            settingsParms.MLX90640FPS = v;
                            settings_write_all();
                            mlx90640_flushRate();
                            lowQualityRender = (v == 6);
                            done = true;
                        }
                        if (b2 & RENDER_Wheel_Back) done = true;
                        // Force redraw main menu bg next loop
                    }
                } break;
                case MENU_SET_MIN_TEMP: {
                    float v = settingsParms.minTempNew;
                    bool done = false;
                    while (!done) {
                        char buf[16]; snprintf(buf, 16, "%.1f C", v);
                        draw_adjust_overlay("MIN TEMP", buf, "UP/DN:Adj OK:Save");
                        dispcolor_Update();

                        EventBits_t b2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                        if (b2 & RENDER_Encoder_Up) v += 1.0f;
                        if (b2 & RENDER_Encoder_Down) v -= 1.0f;
                        if (v > settingsParms.maxTempNew - MIN_TEMPSCALE_DELTA) v = settingsParms.maxTempNew - MIN_TEMPSCALE_DELTA;
                        if (v < -50.0f) v = -50.0f;
                        if (b2 & RENDER_Wheel_Confirm) {
                            settingsParms.minTempNew = v;
                            settings_write_all();
                            done = true;
                        }
                        if (b2 & RENDER_Wheel_Back) done = true;
                    }
                } break;
                case MENU_SET_PALETTE_CENTER: {
                    uint8_t v = settingsParms.PaletteCenterPercent;
                    bool done = false;
                    while (!done) {
                        char buf[16]; snprintf(buf, 16, "%d %%", v);
                        draw_adjust_overlay("PALETTE CTR", buf, "UP/DN:Adj OK:Save");
                        dispcolor_Update();

                        EventBits_t b2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                        if (b2 & RENDER_Encoder_Up && v < 100) v++;
                        if (b2 & RENDER_Encoder_Down && v > 0) v--;
                        if (b2 & RENDER_Wheel_Confirm) {
                            settingsParms.PaletteCenterPercent = v;
                            settings_write_all();
                            done = true;
                        }
                        if (b2 & RENDER_Wheel_Back) done = true;
                    }
                } break;
                case MENU_SET_MAX_TEMP: {
                    float v = settingsParms.maxTempNew;
                    bool done = false;
                    while (!done) {
                        char buf[16]; snprintf(buf, 16, "%.1f C", v);
                        draw_adjust_overlay("MAX TEMP", buf, "UP/DN:Adj OK:Save");
                        dispcolor_Update();

                        EventBits_t b2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                        if (b2 & RENDER_Encoder_Up) v += 1.0f;
                        if (b2 & RENDER_Encoder_Down) v -= 1.0f;
                        if (v < settingsParms.minTempNew + MIN_TEMPSCALE_DELTA) v = settingsParms.minTempNew + MIN_TEMPSCALE_DELTA;
                        if (v > 500.0f) v = 500.0f;
                        if (b2 & RENDER_Wheel_Confirm) {
                            settingsParms.maxTempNew = v;
                            settings_write_all();
                            done = true;
                        }
                        if (b2 & RENDER_Wheel_Back) done = true;
                    }
                } break;
                case MENU_REALTIME_ANALYSIS:
                    settingsParms.RealTimeAnalysis = !settingsParms.RealTimeAnalysis;
                    settings_write_all();
                    break;
                case MENU_VIEW_SCREENSHOTS: {
                    // Simple viewer - Redraws logic to match style
                    char fileList[20][32];
                    int fileCount = save_listBmpFiles(fileList, 20);
                    if (fileCount <= 0) {
                         draw_adjust_overlay("GALLERY", "Empty", "No files found");
                         dispcolor_Update();
                         vTaskDelay(1000 / portTICK_PERIOD_MS);
                    } else {
                        int viewIndex = 0;
                        bool viewDone = false;
                        while (!viewDone) {
                            draw_adjust_overlay("GALLERY", fileList[viewIndex], "ENC:Sel OK:View");
                             // Add counter
                            char ctr[32]; snprintf(ctr, sizeof(ctr), "%d/%d", viewIndex+1, fileCount);
                            int16_t cx = (screen_w - 200)/2 + 180;
                            dispcolor_printf(cx, (screen_h-80)/2 + 4, FONTID_6X8M, C_BLACK, "%s", ctr);
                            dispcolor_Update();

                            EventBits_t b2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                            if (b2 & RENDER_Encoder_Up) { if (viewIndex > 0) viewIndex--; else viewIndex = fileCount - 1; }
                            if (b2 & RENDER_Encoder_Down) { if (viewIndex < fileCount - 1) viewIndex++; else viewIndex = 0; }
                            if (b2 & RENDER_Wheel_Confirm) {
                                dispcolor_FillRect(0, 0, screen_w, screen_h, C_BLACK);
                                if (save_viewBmpFile(fileList[viewIndex]) == 0) {
                                    dispcolor_printf(4, screen_h - 12, FONTID_6X8M, C_WHITE, "%s", fileList[viewIndex]);
                                    dispcolor_Update();
                                    xEventGroupWaitBits(pHandleEventGroup, RENDER_Wheel_Back | RENDER_Wheel_Confirm, pdTRUE, pdFALSE, portMAX_DELAY);
                                }
                            }
                            if (b2 & RENDER_Wheel_Back) viewDone = true;
                        }
                    }
                } break;
                case MENU_DELETE_SCREENSHOTS: {
                     // Similar style for delete
                     // ... (Abbreviated for length, follows same pattern) ...
                     char fileList[20][32];
                     int fileCount = save_listBmpFiles(fileList, 20);
                     if (fileCount <= 0) {
                         draw_adjust_overlay("DELETE", "Empty", "No files");
                         dispcolor_Update();
                         vTaskDelay(1000 / portTICK_PERIOD_MS);
                     } else {
                         int delIndex = 0;
                         bool delDone = false;
                         while(!delDone) {
                             char* fname = (delIndex < fileCount) ? fileList[delIndex] : "ALL FILES";
                             draw_adjust_overlay("DELETE FILE", fname, "OK:Delete L:Back");
                             if (delIndex == fileCount) dispcolor_printf((screen_w-200)/2 + 20, (screen_h-80)/2 + 30, FONTID_16F, 0xF800, "ALL FILES"); // Red text for ALL
                             dispcolor_Update();

                             EventBits_t b2 = xEventGroupWaitBits(pHandleEventGroup, RENDER_Encoder_Up | RENDER_Encoder_Down | RENDER_Wheel_Confirm | RENDER_Wheel_Back, pdTRUE, pdFALSE, portMAX_DELAY);
                             if (b2 & RENDER_Encoder_Up) { if (delIndex > 0) delIndex--; else delIndex = fileCount; }
                             if (b2 & RENDER_Encoder_Down) { if (delIndex < fileCount) delIndex++; else delIndex = 0; }
                             if (b2 & RENDER_Wheel_Confirm) {
                                 if (delIndex < fileCount) save_deleteBmpFile(fileList[delIndex]);
                                 else save_deleteAllBmpFiles();
                                 fileCount = save_listBmpFiles(fileList, 20);
                                 if (fileCount <= 0) delDone = true;
                                 else delIndex = 0;
                             }
                             if (b2 & RENDER_Wheel_Back) delDone = true;
                         }
                     }
                } break;
            }
        }
    }

    dispcolor_FillRect(0, 0, screen_w, screen_h, C_BLACK);
    dispcolor_Update();
    return 0;
}