#ifndef _THERMALIMAGING_H
#define _THERMALIMAGING_H

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 基本功能 (简化版本只保留必要的)
#include "console.h"
#include "func.h"
#include "settings.h"

// LCD显示
#include "dispcolor.h"
#include "f16f.h"
#include "f24f.h"
#include "f32f.h"
#include "f6x8m.h"
#include "font.h"
#include "st7789.h"

// IIC和MLX90640热成像传感器
#include "MLX90640_I2C_Driver.h"
#include "driver_MLX90640.h"
#include "iic.h"

// Task任务
#include "buttons_task.h"
#include "render_task.h"
#include "mlx90640_task.h"

#endif // _THERMALIMAGING_H