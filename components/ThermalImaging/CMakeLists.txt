set(ThermalImaging_srcs   
    "src/console.c"   
    "src/func.c"
    "src/menu.c"
    "src/simple_menu.c"
    "src/save.c"
    "src/messagebox.c"
    "src/settings.c"
    "src/sleep.c"
    "src/tools/tools.c"
)

set(task_srcs
    "src/task/render_task_simple.c" 
    "src/task/buttons_task.c"
    "src/task/wheel_task.c"
    "src/task/siq02_test.c"
    "src/task/mlx90640_task.c"
)

set(lcd_srcs 
    "src/lcd/spi_lcd.c"
    "src/lcd/lcd_bl.c"
    "src/lcd/dispcolor.c"
    "src/lcd/st7789.c"
    "src/lcd/font/f32f.c"
    "src/lcd/font/f6x8m.c"
    "src/lcd/font/f16f.c"
    "src/lcd/font/f24f.c"
    "src/lcd/font/font.c"
)

set(iic_srcs
    "src/iic/iic.c"
    "src/iic/driver_MLX90640.c"
    "src/iic/MLX90640_I2C_Driver.c"
)

set(interpolation_srcs
    "src/interpolation/palette.c"
    "src/interpolation/Bilinear.c"
    "src/interpolation/Gauss.c"
)

idf_component_register(SRCS "${ThermalImaging_srcs}" "${lcd_srcs}" "${task_srcs}" "${iic_srcs}" "${interpolation_srcs}"
                       INCLUDE_DIRS "include" "include/lcd" "include/tasks" "include/tools" "include/iic"
                       REQUIRES driver esp_adc spi_flash nvs_flash esp_wifi
                       PRIV_REQUIRES esp_psram spiffs)
