# hotimage_code_master — Thermal camera example

这是基于 ESP-IDF 的热像（MLX90640）演示工程。README 已更新，列出最近加入或修改的重要功能、运行操作以及常见配置项位置，方便调试和继续开发。

**主要功能（快速概览）
**
- 实时热像显示 (MLX90640) → ST7789 SPI 屏幕输出。
- 两种渲染模式：高质量（高斯 + 双线性）与低质量（最近邻，低延时）——可在菜单中为 32Hz 切换低质量渲染以提高帧率。
- 自动/手动量程（AutoScale）：自动根据帧内 min/max 调整伪彩色映射；在 `auto_scale` 子项按确认可把当前帧范围写为固定量程并关闭自动刻度。
- UI 改进：主界面标题栏与底部数据栏选中时使用灰色背景高亮；在子项选择中被选中文本变为黑色以便在灰色上可读。
- 可移动十字线（Crosshair）：右拨在图像区进入十字线移动模式；位置持久化到设置。
- 运行时睡眠与真正的深度睡眠：实现了运行时 sleep（关闭传感器和屏幕）以及 `system_enter_deep_sleep()`（调用 esp_deep_sleep_start，需正确配置 RTC 唤醒引脚）。
- 启动状态灯：GPIO21 简单闪烁表明设备上电。

**常用文件与设置位置**
- 主渲染与 UI：`components/ThermalImaging/src/task/render_task_simple.c`
	- 量程保护（MIN_DISPLAY_RANGE）、低量程平滑与中值滤波、UI 高亮与子项确认逻辑都在此文件。
- 菜单相关：`components/ThermalImaging/src/simple_menu.c`
	- MLX FPS（16Hz / 32Hz）选择位置、低质量渲染开关、以及进入运行时睡眠的菜单项（如果启用）。
- 显示驱动：`components/ThermalImaging/src/lcd/st7789.c`
	- 屏幕开/关命令（`st7789_DisplayOn` / `st7789_DisplayOff`）、MADCTL（屏幕方向）可在此调整。
- Sleep 管理：`components/ThermalImaging/src/sleep.c` 与 `include/sleep.h`
	- 包含 runtime sleep/wake 与 `system_enter_deep_sleep()` 的实现（ext0/ext1 配置需按硬件选择 RTC pin）。
- 设置持久化 (NVS)：`components/ThermalImaging/src/settings.c` / `include/settings.h`
	- 结构体 `settingsParms` 包含 `AutoScaleMode`, `minTempNew`, `maxTempNew`, `PaletteCenterPercent`, `ColorScale`, `CrossX`, `CrossY` 等。

**运行与调试（PowerShell）**
- 构建工程：

```powershell
idf.py fullclean
idf.py build
```

- 烧写固件（修改为你实际的串口端口）：

```powershell
idf.py -p COM3 flash
```

- 串口 monitor：

```powershell
idf.py -p COM3 monitor
```

**常用操作（UI 交互）**
- 旋钮（Wheel / Encoder）：
	- 右拨/确认 (`Wheel Confirm`)：
		- 在图像区：进入十字线移动模式（再次右拨切换 X/Y 轴）。
		- 在标题子项 `fix_scale` 上确认：把当前帧的真实 min/max 写入为固定量程并关闭 AutoScale（会持久化）。
	- 左拨 (`Wheel Back`)：
		- 在十字线模式退出并保存位置到设置；在子项/子模式退出到上一级；在焦点模式进入菜单。
	- 编码器旋转：切换子项 / 调整参数（视当前模式）。

**行为说明与调优建议**
- 如果你在空场景（例如室温恒定）看到颜色过于极端（红/蓝），这是因为自动刻度会把小幅温差拉满到调色板。已加入fix scale

- 固定量程（Fix scale）行为：当在标题的 `fix_scale` 子项按确认时，系统会把最近一帧的原始 `min/max`（来自 MLX 帧数据）写入 `settingsParms.minTempNew` / `settingsParms.maxTempNew` 并关闭 `AutoScaleMode`，随后设置会被 `settings_write_all()` 持久化。

- 深度睡眠注意事项：`system_enter_deep_sleep()` 会调用 `esp_deep_sleep_start()` 并配置 ext0/ext1 唤醒，必须确认使用的 GPIO 为 RTC 可用引脚并正确设置唤醒电平/上拉下拉以保证可靠唤醒和低功耗。

**开发者提示**
- 更改屏幕方向（MADCTL）请在 `st7789.c` 中修改对应寄存器值，并测试绘图对齐。
- 若希望用户可配置去噪/最小量程，可把这些参数加入 `settingsParms` 并在菜单中暴露。
- 若需要更强的噪声去除（但更慢），可替换为多帧时间中值或连通域过滤（需要额外内存/CPU）。

---

如果你希望我把 README 翻成英文、加入项目徽标、或把 MIN_DISPLAY_RANGE / 去噪选项加入设置菜单并实现 UI，我可以继续修改代码并提交对应的变更.
