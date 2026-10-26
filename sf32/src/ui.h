/*
 * LVGL user interface for the 480x480 AMOLED, display buttons and keypad.
 *
 *   A (PA34) click : open / close help (next field if keypad unavailable)
 *   A (PA34) hold  : transmit now
 *   B (PA35) click : next value / temperature +0.5
 *   B (PA35) hold  : jump to the previous field
 *   C (PA33) click : previous value / temperature -0.5
 *   C (PA33) hold  : toggle power
 *
 * Keypad: * / # temperature down / up, 0 power, 1 mode, 2 fan,
 * 3 Powerful, 4 / 5 swing V / H, 6 Econo, 7 Quiet, 8 Eye, 9 Comfort.
 * PREV / NEXT select a field; ENTER / DEL change its value;
 * HOME selects temperature, MAIL sends, STOP turns the AC off,
 * ESC opens / closes the on-screen key guide.
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UI_EVT_NEXT_FIELD,
    UI_EVT_PREV_FIELD,
    UI_EVT_VALUE_UP,
    UI_EVT_VALUE_DOWN,
    UI_EVT_SEND,
    UI_EVT_TOGGLE_POWER,
    UI_EVT_HOME,
    UI_EVT_TOGGLE_HELP,
    UI_EVT_POWER_OFF,
    UI_EVT_TEMP_UP,
    UI_EVT_TEMP_DOWN,
    UI_EVT_CYCLE_MODE,
    UI_EVT_CYCLE_FAN,
    UI_EVT_TOGGLE_SWING_V,
    UI_EVT_TOGGLE_SWING_H,
    UI_EVT_TOGGLE_POWERFUL,
    UI_EVT_TOGGLE_ECONO,
    UI_EVT_TOGGLE_QUIET,
    UI_EVT_TOGGLE_SENSOR,
    UI_EVT_TOGGLE_COMFORT,
} ui_event_t;

/* Initialise LVGL (littlevgl2rtt) and build the screen. Returns 0 on success. */
int ui_init(void);

/* Start the LVGL render thread. */
void ui_start(void);

/* Post an event from any context (button callback, shell). */
void ui_post(ui_event_t evt);

/* Ask the UI thread to redraw from the current state. */
void ui_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
