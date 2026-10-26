/* Test the actual dispatcher, including its static helpers, without exposing
 * test-only production APIs. Unused screen/thread code is linker-discarded. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/ui.c"

static daikin_state_t state;
static unsigned changes;
static unsigned sends;
static int lock_depth;
static char status_text[128];

void ac_ctrl_lock(void) { ++lock_depth; }
void ac_ctrl_unlock(void) { assert(lock_depth > 0); --lock_depth; }
daikin_state_t *ac_ctrl_state(void) { assert(lock_depth > 0); return &state; }
void ac_ctrl_mark_changed(void) { assert(lock_depth == 0); ++changes; }
int ac_ctrl_send(void) { assert(lock_depth == 0); ++sends; return 0; }
uint32_t ac_ctrl_send_count(void) { return sends; }
bool ac_ctrl_last_sent_power(void) { return state.power; }
ir_power_t ir_power_state(void) { return IR_POWER_OK; }
rt_tick_t rt_tick_get(void) { return 100; }
rt_tick_t rt_tick_from_millisecond(int ms) { return (rt_tick_t)ms; }

void lv_label_set_text(lv_obj_t *obj, const char *text)
{
    (void)obj;
    snprintf(status_text, sizeof(status_text), "%s", text);
}
void lv_obj_set_style_text_color(lv_obj_t *obj, lv_color_t color, int selector)
{
    (void)obj; (void)color; (void)selector;
}
void lv_obj_add_flag(lv_obj_t *obj, int flag) { (void)obj; (void)flag; }
void lv_obj_clear_flag(lv_obj_t *obj, int flag) { (void)obj; (void)flag; }
void lv_obj_remove_flag(lv_obj_t *obj, int flag) { (void)obj; (void)flag; }

static void reset(void)
{
    daikin_state_init(&state);
    changes = sends = 0;
    lock_depth = 0;
    s_field = F_POWER;
    s_help_visible = false;
    s_flash_until = 0;
    s_last_send_count = 0;
    status_text[0] = '\0';
}

static void dispatch(ui_event_t event)
{
    s_dirty = false;
    handle_event(event);
    assert(lock_depth == 0);
    assert(s_dirty);
}

static void test_shortcuts_ignore_cursor(void)
{
    /* Start every shortcut with every possible cursor position. Compare the
     * complete AC state so an accidental selected-field edit cannot pass. */
    static const ui_event_t events[] = {
        UI_EVT_TOGGLE_POWER, UI_EVT_TEMP_UP, UI_EVT_TEMP_DOWN,
        UI_EVT_CYCLE_MODE, UI_EVT_CYCLE_FAN, UI_EVT_TOGGLE_SWING_V,
        UI_EVT_TOGGLE_SWING_H, UI_EVT_TOGGLE_POWERFUL, UI_EVT_TOGGLE_ECONO,
        UI_EVT_TOGGLE_QUIET, UI_EVT_TOGGLE_SENSOR, UI_EVT_TOGGLE_COMFORT,
    };

    for (int field = 0; field < F_COUNT; ++field)
    {
        for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
        {
            reset();
            s_field = (field_t)field;
            daikin_state_t expected = state;
            switch (events[i])
            {
            case UI_EVT_TOGGLE_POWER: expected.power = true; break;
            case UI_EVT_TEMP_UP: expected.temp_half_deg = 51; break;
            case UI_EVT_TEMP_DOWN: expected.temp_half_deg = 49; break;
#ifdef REMOTE_HEAT_PUMP
            case UI_EVT_CYCLE_MODE: expected.mode = DAIKIN_MODE_HEAT; break;
#else
            case UI_EVT_CYCLE_MODE: expected.mode = DAIKIN_MODE_FAN; break;
#endif
            case UI_EVT_CYCLE_FAN: expected.fan = DAIKIN_FAN_QUIET; break;
            case UI_EVT_TOGGLE_SWING_V: expected.swing_v = true; break;
            case UI_EVT_TOGGLE_SWING_H: expected.swing_h = true; break;
            case UI_EVT_TOGGLE_POWERFUL: expected.powerful = true; break;
            case UI_EVT_TOGGLE_ECONO: expected.econo = true; break;
            case UI_EVT_TOGGLE_QUIET: expected.quiet = true; break;
            case UI_EVT_TOGGLE_SENSOR: expected.sensor = true; break;
            case UI_EVT_TOGGLE_COMFORT: expected.comfort = true; break;
            default: assert(false);
            }
            dispatch(events[i]);
            assert(memcmp(&state, &expected, sizeof(state)) == 0);
            assert(changes == 1);
            assert(sends == 0);
        }
    }
}

static void test_temperature_and_cycles(void)
{
    reset();
    state.temp_half_deg = 36;
    dispatch(UI_EVT_TEMP_DOWN);
    assert(state.temp_half_deg == 36);
    assert(changes == 0);
    state.temp_half_deg = 64;
    dispatch(UI_EVT_TEMP_UP);
    assert(state.temp_half_deg == 64);
    assert(changes == 0);
    dispatch(UI_EVT_TEMP_DOWN);
    assert(state.temp_half_deg == 63);
    assert(changes == 1);

    state.mode = DAIKIN_MODE_FAN;
    dispatch(UI_EVT_CYCLE_MODE);
#ifdef REMOTE_HEAT_PUMP
    assert(state.mode == DAIKIN_MODE_AUTO);
#else
    assert(state.mode == DAIKIN_MODE_DRY);
#endif
    state.fan = DAIKIN_FAN_5;
    dispatch(UI_EVT_CYCLE_FAN);
    assert(state.fan == DAIKIN_FAN_AUTO);
}

static void test_features_and_power(void)
{
    reset();
    state.econo = state.quiet = true;
    dispatch(UI_EVT_TOGGLE_POWERFUL);
    assert(state.powerful && !state.econo && !state.quiet);
    dispatch(UI_EVT_TOGGLE_ECONO);
    assert(!state.powerful && state.econo);
    dispatch(UI_EVT_TOGGLE_QUIET);
    assert(state.econo && state.quiet);
    dispatch(UI_EVT_TOGGLE_POWERFUL);
    dispatch(UI_EVT_TOGGLE_QUIET);
    assert(!state.powerful && !state.econo && state.quiet);

    dispatch(UI_EVT_TOGGLE_POWER);
    assert(state.power);
    dispatch(UI_EVT_POWER_OFF);
    assert(!state.power);
    unsigned before_stop = changes;
    dispatch(UI_EVT_POWER_OFF);
    assert(!state.power);
    assert(changes == before_stop + 1); /* STOP retries OFF even if already off. */
    dispatch(UI_EVT_TOGGLE_POWER);
    assert(state.power);
    dispatch(UI_EVT_TOGGLE_POWER);
    assert(!state.power);
    assert(sends == 0);
}

static void test_navigation_and_send(void)
{
    reset();
    daikin_state_t initial = state;
    dispatch(UI_EVT_PREV_FIELD);
    assert(s_field == F_COUNT - 1);
    dispatch(UI_EVT_NEXT_FIELD);
    assert(s_field == F_POWER);
    dispatch(UI_EVT_TOGGLE_HELP);
    assert(s_help_visible);
    dispatch(UI_EVT_TOGGLE_HELP);
    assert(!s_help_visible);
    dispatch(UI_EVT_TOGGLE_HELP);
    dispatch(UI_EVT_HOME);
    assert(s_field == F_TEMP && !s_help_visible);
    assert(memcmp(&state, &initial, sizeof(state)) == 0);
    assert(changes == 0 && sends == 0);

    dispatch(UI_EVT_SEND);
    assert(sends == 1 && changes == 0);
    assert(strcmp(status_text, "SENT: OFF") == 0);
    assert(s_last_send_count == sends);

    dispatch(UI_EVT_TOGGLE_HELP);
    dispatch(UI_EVT_TEMP_UP);
    assert(!s_help_visible && state.temp_half_deg == 51);
    assert(changes == 1 && sends == 1);

    /* The display buttons still adjust whichever field is selected. */
    s_field = F_SWING_H;
    dispatch(UI_EVT_VALUE_UP);
    assert(state.swing_h);
    dispatch(UI_EVT_VALUE_DOWN);
    assert(!state.swing_h);
    s_field = F_TEMP;
    dispatch(UI_EVT_VALUE_DOWN);
    assert(state.temp_half_deg == 50);
}

int main(void)
{
    test_shortcuts_ignore_cursor();
    test_temperature_and_cycles();
    test_features_and_power();
    test_navigation_and_send();
#ifdef REMOTE_HEAT_PUMP
    puts("SF32 UI keypad tests passed (heat pump)");
#else
    puts("SF32 UI keypad tests passed (cooling only)");
#endif
    return 0;
}
