/* TCA8418 keypad on the LilyGo T-SF32-Keyboard expansion board. */
#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start forwarding keypad presses to the UI event queue. Call once after
 * ir_power_init() and ui_init(). Returns 0 on success; display buttons
 * remain usable if the keyboard board is absent. */
int keypad_init(void);

/* True once the keyboard is open and its UI forwarding thread is running. */
bool keypad_available(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYPAD_H */
