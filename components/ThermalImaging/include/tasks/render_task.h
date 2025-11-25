#ifndef MAIN_TASK_UI_H_
#define MAIN_TASK_UI_H_

#include "esp_system.h"
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

extern EventGroupHandle_t pHandleEventGroup;
extern SemaphoreHandle_t pSPIMutex;

typedef enum _render_type {
    RENDER_MLX90640_NO0 = 1 << 0, // 第0帧
    RENDER_MLX90640_NO1 = 1 << 1, // 第1帧
    RENDER_ShortPress_Up = 1 << 2, // Up按钮 (保留兼容)
    RENDER_Hold_Up = 1 << 3, // Up按钮长按 (保留兼容)
    RENDER_ShortPress_Center = 1 << 4, // Center按钮 (保留兼容)
    RENDER_Hold_Center = 1 << 5, // Center按钮长按 (保留兼容)
    RENDER_ShortPress_Down = 1 << 6, // Down按钮 (保留兼容)
    RENDER_Hold_Down = 1 << 7, // Down按钮长按 (保留兼容)
    // Wheel 滚轮事件 (ADC on IO17)
    RENDER_Wheel_Back = 1 << 8,     // wheel 左拨 = 返回/取消
    RENDER_Wheel_Confirm = 1 << 9,  // wheel 右拨 = 确认/进入
    RENDER_Wheel_Press = 1 << 10,   // wheel 按下
    // SIQ02 编码器事件
    RENDER_Encoder_Up = 1 << 11,    // 编码器左转 = 值增加/向上
    RENDER_Encoder_Down = 1 << 12,  // 编码器右转 = 值减少/向下
    RENDER_Encoder_Press = 1 << 13, // 编码器按下 = 确认
} render_type;

void render_task(void* arg);

void tips_printf(const char* args, ...);

#endif /* MAIN_TASK_UI_H_ */
