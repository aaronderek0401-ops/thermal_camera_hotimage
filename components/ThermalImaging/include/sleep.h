#ifndef SLEEP_H_
#define SLEEP_H_

#include <stdbool.h>

void Deep_Sleep_Run(void);

// Simple runtime sleep manager: pause sensor and turn off display.
void system_enter_sleep(void);
void system_exit_sleep(void);
bool system_is_sleeping(void);
// Enter deep sleep (device resets on wake). Uses DEEP_SLEEP_WAKE_PIN (RTC-capable).
void system_enter_deep_sleep(void);

#endif /* SLEEP_H_ */
