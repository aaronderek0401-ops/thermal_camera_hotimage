
	# hot image — Thermal camera example

	<p align="center"> <img src="assets/logo.svg" alt="hot image logo" width="280"/></p>

	Platform: ESP32-S3 (esp32s3)

	This is an example thermal camera project based on ESP-IDF using the MLX90640 sensor and an ST7789 SPI display. The README documents recent features, where to find key code, and basic build/run instructions to help you debug and extend the project.

	本项目基于 ESP-IDF，使用 MLX90640 热像传感器和 ST7789 SPI 屏幕。README 列出最近的功能、关键代码位置以及基本的构建/运行说明，方便调试与扩展。

	**Key Features (Quick Overview)**

	- Real-time thermal imaging with MLX90640 rendered to an ST7789 SPI display.
	- Two rendering modes: High-Quality (Gaussian + bilinear) and Low-Quality (nearest-neighbor, lower latency). Low-quality mode is intended for 32Hz operation.
	- Auto/Manual temperature scaling (AutoScale). The `fix_scale` subitem allows you to write the current frame's min/max to settings and disable AutoScale.
	- UI improvements: the main title bar and bottom data bar now use a gray background for focus highlighting; selected subitem text becomes black on gray for readability.
	- Movable crosshair: right-press (wheel confirm) in the image area enters crosshair move mode; position is persisted to settings.
	- Runtime sleep and deep sleep: runtime sleep pauses sensor and turns off the display/backlight; `system_enter_deep_sleep()` configures RTC wake and calls `esp_deep_sleep_start()` (select an RTC-capable wake pin).
	- Boot status LED: GPIO21 blinks briefly at power-up.

	**主要功能（快速概览）**

	- 实时热像显示（MLX90640）并在 ST7789 SPI 屏幕上输出。
	- 两种渲染模式：高质量（高斯 + 双线性）与低质量（最近邻，低延时）。低质量模式适用于 32Hz 的高帧率渲染。
	- 自动/手动量程（AutoScale）。`fix_scale` 子项可以把当前帧的 min/max 写入设置并关闭 AutoScale。
	- UI 改进：主界面标题栏与底部数据栏使用灰色背景进行焦点高亮；子项选中文本在灰色上显示为黑色以便阅读。
	- 可移动十字线：在图像区按下旋钮确认进入十字线移动模式；位置会持久化保存到设置。
	- 运行时睡眠与深度睡眠：运行时睡眠会暂停传感器并关闭屏幕/背光；`system_enter_deep_sleep()` 会配置 RTC 唤醒并调用 `esp_deep_sleep_start()`（需要选择 RTC 支持的唤醒引脚）。
	- 启动状态灯：GPIO21 在上电时短暂闪烁表示设备已上电。

	**Key Files & Where to Look**

	- Main rendering & UI logic: `components/ThermalImaging/src/task/render_task_simple.c`
		- Contains MIN_DISPLAY_RANGE protection, low-range temporal smoothing + median filter, UI highlighting, and the logic that writes fixed scale when confirming the `fix_scale` subitem.
	- Menu: `components/ThermalImaging/src/simple_menu.c`
		- Where MLX FPS (16Hz / 32Hz) options, the low-quality render toggle, and runtime-sleep menu item live (if enabled).
	- Display driver: `components/ThermalImaging/src/lcd/st7789.c`
		- Screen on/off helpers (`st7789_DisplayOn` / `st7789_DisplayOff`) and MADCTL (orientation) adjustments.
	- Sleep management: `components/ThermalImaging/src/sleep.c` and `include/sleep.h`
		- Runtime sleep/wake and `system_enter_deep_sleep()` (ext0/ext1 config depends on the chosen RTC pin).
	- Settings (NVS): `components/ThermalImaging/src/settings.c` / `include/settings.h`
		- `settingsParms` holds `AutoScaleMode`, `minTempNew`, `maxTempNew`, `PaletteCenterPercent`, `ColorScale`, `CrossX`, `CrossY`, etc.

	**常用文件与设置位置**

	- 主渲染与 UI：`components/ThermalImaging/src/task/render_task_simple.c`
		- 包含 MIN_DISPLAY_RANGE 保护、低量程时的时间平滑与中值滤波、UI 高亮，以及在 `fix_scale` 子项确认时写入固定量程的逻辑。
	- 菜单：`components/ThermalImaging/src/simple_menu.c`
		- MLX FPS（16Hz / 32Hz）、低质量渲染开关，以及运行时睡眠菜单项的位置（如果启用）。
	- 显示驱动：`components/ThermalImaging/src/lcd/st7789.c`
		- 屏幕开/关函数（`st7789_DisplayOn` / `st7789_DisplayOff`）以及 MADCTL（方向）设置。
	- Sleep 管理：`components/ThermalImaging/src/sleep.c` 与 `include/sleep.h`
		- 运行时睡眠/唤醒与 `system_enter_deep_sleep()`（ext0/ext1 配置需根据所选 RTC 引脚而定）。
	- 设置持久化 (NVS)：`components/ThermalImaging/src/settings.c` / `include/settings.h`
		- `settingsParms` 包含 `AutoScaleMode`、`minTempNew`、`maxTempNew`、`PaletteCenterPercent`、`ColorScale`、`CrossX`、`CrossY` 等字段。

	**Build & Flash (PowerShell)**

	Build the project:

	```powershell
	idf.py fullclean
	idf.py build
	```

	Flash the firmware (replace `COM3` with your serial port):

	```powershell
	idf.py -p COM3 flash
	```

	Open serial monitor:

	```powershell
	idf.py -p COM3 monitor
	```

	**运行与调试（PowerShell）**

	构建工程：

	```powershell
	idf.py fullclean
	idf.py build
	```

	烧写固件（请替换为实际串口）：

	```powershell
	idf.py -p COM3 flash
	```

	串口监视：

	```powershell
	idf.py -p COM3 monitor
	```

	**UI Controls (How to use)**

	- Encoder (wheel):
		- Wheel Confirm / Right-press:
			- In the image area: enter crosshair move mode (press again to toggle X/Y axis).
			- On the `fix_scale` title subitem: confirm writes the current frame's raw min/max into settings and disables AutoScale (persisted by `settings_write_all()`).
		# hotimage_code_master — Thermal camera example

		Platform: ESP32-S3 (esp32s3)

		This is an example thermal camera project based on ESP-IDF using the MLX90640 sensor and an ST7789 SPI display. The README documents recent features, where to find key code, and basic build/run instructions to help you debug and extend the project.

		本项目基于 ESP-IDF，使用 MLX90640 热像传感器和 ST7789 SPI 屏幕。README 列出最近的功能、关键代码位置以及基本的构建/运行说明，方便调试与扩展。

		**Key Features (Quick Overview)**

		- Real-time thermal imaging with MLX90640 rendered to an ST7789 SPI display.
		- Two rendering modes: High-Quality (Gaussian + bilinear) and Low-Quality (nearest-neighbor, lower latency). Low-quality mode is intended for 32Hz operation.
		- Auto/Manual temperature scaling (AutoScale). The `fix_scale` subitem allows you to write the current frame's min/max to settings and disable AutoScale.
		- UI improvements: the main title bar and bottom data bar now use a gray background for focus highlighting; selected subitem text becomes black on gray for readability.
		- Movable crosshair: right-press (wheel confirm) in the image area enters crosshair move mode; position is persisted to settings.
		- Runtime sleep and deep sleep: runtime sleep pauses sensor and turns off the display/backlight; `system_enter_deep_sleep()` configures RTC wake and calls `esp_deep_sleep_start()` (select an RTC-capable wake pin).
		- Boot status LED: GPIO21 blinks briefly at power-up.

		**主要功能（快速概览）**

		- 实时热像显示（MLX90640）并在 ST7789 SPI 屏幕上输出。
		- 两种渲染模式：高质量（高斯 + 双线性）与低质量（最近邻，低延时）。低质量模式适用于 32Hz 的高帧率渲染。
		- 自动/手动量程（AutoScale）。`fix_scale` 子项可以把当前帧的 min/max 写入设置并关闭 AutoScale。
		- UI 改进：主界面标题栏与底部数据栏使用灰色背景进行焦点高亮；子项选中文本在灰色上显示为黑色以便阅读。
		- 可移动十字线：在图像区按下旋钮确认进入十字线移动模式；位置会持久化保存到设置。
		- 运行时睡眠与深度睡眠：运行时睡眠会暂停传感器并关闭屏幕/背光；`system_enter_deep_sleep()` 会配置 RTC 唤醒并调用 `esp_deep_sleep_start()`（需要选择 RTC 支持的唤醒引脚）。
		- 启动状态灯：GPIO21 在上电时短暂闪烁表示设备已上电。

		**Key Files & Where to Look**

		- Main rendering & UI logic: `components/ThermalImaging/src/task/render_task_simple.c`
			- Contains MIN_DISPLAY_RANGE protection, low-range temporal smoothing + median filter, UI highlighting, and the logic that writes fixed scale when confirming the `fix_scale` subitem.
		- Menu: `components/ThermalImaging/src/simple_menu.c`
			- Where MLX FPS (16Hz / 32Hz) options, the low-quality render toggle, and runtime-sleep menu item live (if enabled).
		- Display driver: `components/ThermalImaging/src/lcd/st7789.c`
			- Screen on/off helpers (`st7789_DisplayOn` / `st7789_DisplayOff`) and MADCTL (orientation) adjustments.
		- Sleep management: `components/ThermalImaging/src/sleep.c` and `include/sleep.h`
			- Runtime sleep/wake and `system_enter_deep_sleep()` (ext0/ext1 config depends on the chosen RTC pin).
		- Settings (NVS): `components/ThermalImaging/src/settings.c` / `include/settings.h`
			- `settingsParms` holds `AutoScaleMode`, `minTempNew`, `maxTempNew`, `PaletteCenterPercent`, `ColorScale`, `CrossX`, `CrossY`, etc.

		**常用文件与设置位置**

		- 主渲染与 UI：`components/ThermalImaging/src/task/render_task_simple.c`
			- 包含 MIN_DISPLAY_RANGE 保护、低量程时的时间平滑与中值滤波、UI 高亮，以及在 `fix_scale` 子项确认时写入固定量程的逻辑。
		- 菜单：`components/ThermalImaging/src/simple_menu.c`
			- MLX FPS（16Hz / 32Hz）、低质量渲染开关，以及运行时睡眠菜单项的位置（如果启用）。
		- 显示驱动：`components/ThermalImaging/src/lcd/st7789.c`
			- 屏幕开/关函数（`st7789_DisplayOn` / `st7789_DisplayOff`）以及 MADCTL（方向）设置。
		- Sleep 管理：`components/ThermalImaging/src/sleep.c` 与 `include/sleep.h`
			- 运行时睡眠/唤醒与 `system_enter_deep_sleep()`（ext0/ext1 配置需根据所选 RTC 引脚而定）。
		- 设置持久化 (NVS)：`components/ThermalImaging/src/settings.c` / `include/settings.h`
			- `settingsParms` 包含 `AutoScaleMode`、`minTempNew`、`maxTempNew`、`PaletteCenterPercent`、`ColorScale`、`CrossX`、`CrossY` 等字段。

		**Build & Flash (PowerShell)**

		Build the project:

		```powershell
		idf.py fullclean
		idf.py build
		```

		Flash the firmware (replace `COM3` with your serial port):

		```powershell
		idf.py -p COM3 flash
		```

		Open serial monitor:

		```powershell
		idf.py -p COM3 monitor
		```

		**运行与调试（PowerShell）**

		构建工程：

		```powershell
		idf.py fullclean
		idf.py build
		```

		烧写固件（请替换为实际串口）：

		```powershell
		idf.py -p COM3 flash
		```

		串口监视：

		```powershell
		idf.py -p COM3 monitor
		```

		**UI Controls (How to use)**

		- Encoder (wheel):
			- Wheel Confirm / Right-press:
				- In the image area: enter crosshair move mode (press again to toggle X/Y axis).
				- On the `fix_scale` title subitem: confirm writes the current frame's raw min/max into settings and disables AutoScale (persisted by `settings_write_all()`).
			- Wheel Back / Left-press:
				- Exit crosshair mode and save position; exit subitem/mode to the previous level; in focus mode, enter the menu.
			- Rotation: navigate items or adjust values depending on context.

		**常用操作（UI 交互）**

		- 旋钮（Wheel / Encoder）：
			- 右拨 / 确认：
				- 在图像区：进入十字线移动模式（再次确认切换 X/Y）。
				- 在标题的 `fix_scale` 子项确认：把当前帧的原始 min/max 写入设置并关闭 AutoScale（由 `settings_write_all()` 持久化）。
			- 左拨 / 返回：
				- 退出十字线并保存位置；退出子项或子模式回上一级；焦点模式下进入菜单。
			- 旋转：在菜单中切换或调整参数。

		**Behavior Notes & Tuning Tips**

		- If you see overly extreme colors (very red/blue) in a near-uniform scene, that is caused by AutoScale mapping small temperature ranges to the full color palette. Use the `fix_scale` subitem to capture a representative frame and make that the fixed display range.
		- Fix-scale behavior: confirming `fix_scale` writes the last raw frame's min/max (from the MLX data) into `settingsParms.minTempNew` / `settingsParms.maxTempNew`, sets `AutoScaleMode=false`, and persists settings with `settings_write_all()`.
		- Deep sleep: `system_enter_deep_sleep()` calls `esp_deep_sleep_start()` after configuring ext0/ext1 wake. You must select an RTC-capable GPIO and the correct wake polarity/pull to ensure low-power sleep and reliable wake.

		**行为说明与调优建议**

		- 若在近乎均匀的场景中看到颜色非常极端（偏红或偏蓝），这是 AutoScale 将小的温差放大到整个调色板造成的。可用 `fix_scale` 子项抓取代表帧并将其设为固定量程。
		- 固定量程（Fix scale）：确认 `fix_scale` 时，系统会把最近一帧的原始 min/max（来自 MLX 数据）写入 `settingsParms.minTempNew` / `settingsParms.maxTempNew`，把 `AutoScaleMode` 设为 false，并调用 `settings_write_all()` 持久化。
		- 深度睡眠注意事项：`system_enter_deep_sleep()` 在配置 ext0/ext1 唤醒后调用 `esp_deep_sleep_start()`。必须选择 RTC 支持的 GPIO，并设置正确的唤醒电平/上拉下拉，以确保低功耗与可靠唤醒。

		**Developer Notes**

		- To change the screen orientation (MADCTL), edit the register values in `st7789.c` and test drawing alignment.
		- If you want the denoising/min-range parameters to be user-configurable, add them to `settingsParms` and expose them in the menu.
		- For stronger noise removal (but slower), consider multi-frame median or connected-component filtering (requires more memory/CPU).

		**开发者提示**

		- 修改屏幕方向（MADCTL）请在 `st7789.c` 中调整寄存器值并验证绘图对齐。
		- 若希望用户可配置去噪/最小量程，可把这些参数加入 `settingsParms` 并在菜单中暴露。
		- 若需要更强的去噪（但更慢），考虑使用多帧中值或连通域过滤（需要更多内存和 CPU）。

		---

		If you want me to: translate the README to English-only, add a project logo, or implement UI controls to expose `MIN_DISPLAY_RANGE` and denoising options in the settings menu, I can update the code and submit the changes.

		如果你希望我把 README 变成英文版、加入项目徽标，或把 `MIN_DISPLAY_RANGE` / 去噪选项加入设置菜单并实现 UI，我可以继续修改代码并提交相应变更。
