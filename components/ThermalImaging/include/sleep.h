#ifndef SLEEP_H_
#define SLEEP_H_

#include <stdbool.h>

void Deep_Sleep_Run(void);

// Simple runtime sleep manager: pause sensor and turn off display.
void system_enter_sleep(void);
void system_exit_sleep(void);
bool system_is_sleeping(void);

#endif /* SLEEP_H_ */
