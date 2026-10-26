#include "ui.h"

#include <rtthread.h>
#include <stdio.h>
#include <string.h>

#include "littlevgl2rtt.h"
#include "lvgl.h"

#include "ac_ctrl.h"
#include "env_sensor.h"
#include "ir_power.h"
#include "keypad.h"

#define LOG_TAG "ui"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* Mode order of the ARC466A41 (cooling-only remote). Define
 * REMOTE_HEAT_PUMP to cycle through all five modes. */
#ifdef REMOTE_HEAT_PUMP
static const daikin_mode_t k_mode_cycle[] = {DAIKIN_MODE_AUTO, DAIKIN_MODE_DRY, DAIKIN_MODE_COOL,
                                             DAIKIN_MODE_HEAT, DAIKIN_MODE_FAN};
#else
static const daikin_mode_t k_mode_cycle[] = {DAIKIN_MODE_DRY, DAIKIN_MODE_COOL, DAIKIN_MODE_FAN};
#endif
#define MODE_COUNT ((int)(sizeof(k_mode_cycle) / sizeof(k_mode_cycle[0])))

static const daikin_fan_t k_fan_cycle[] = {DAIKIN_FAN_AUTO, DAIKIN_FAN_QUIET, DAIKIN_FAN_1, DAIKIN_FAN_2,
                                           DAIKIN_FAN_3,    DAIKIN_FAN_4,     DAIKIN_FAN_5};
#define FAN_COUNT ((int)(sizeof(k_fan_cycle) / sizeof(k_fan_cycle[0])))

/* The A41's set-point range in COOL: 18.0 - 32.0 C. */
#define UI_MIN_TEMP_HALF 36
#define UI_MAX_TEMP_HALF 64

typedef enum
{
    F_POWER,
    F_MODE,
    F_TEMP,
    F_FAN,
    F_SWING_V,
    F_SWING_H,
    F_POWERFUL,
    F_ECONO,
    F_QUIET,
    F_SENSOR,
    F_COMFORT,
    F_COUNT
} field_t;

#define COLOR_BG 0x000000
#define COLOR_TEXT 0xFFFFFF
#define COLOR_DIM 0x6E6E6E
#define COLOR_ACCENT 0x4FC3F7
#define COLOR_SELECT 0xFFD600
#define COLOR_OK 0x66BB6A
#define COLOR_WARN 0xFFA726
#define COLOR_ERR 0xEF5350

static lv_obj_t *s_title;
static lv_obj_t *s_clock;
static lv_obj_t *s_fields[F_COUNT];
static lv_obj_t *s_unit;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_timer;
static lv_obj_t *s_env;
static lv_obj_t *s_env_sub;
static lv_obj_t *s_help;

static field_t s_field = F_TEMP;
static bool s_help_visible;
static volatile bool s_dirty = true;
static struct rt_mailbox s_mb;
static char s_mb_pool[16 * sizeof(rt_ubase_t)];
static uint32_t s_last_send_count;
static rt_tick_t s_flash_until;
static bool s_last_pending;
static uint16_t s_last_minute = 0xFFFF;
static uint32_t s_env_seq;
static rt_tick_t s_env_tick;

/* True while a transient status message ("SENT") should stay visible. */
static bool flash_active(void)
{
    return s_flash_until != 0 && (int32_t)(rt_tick_get() - s_flash_until) < 0;
}

static int index_of_mode(daikin_mode_t m)
{
    for (int i = 0; i < MODE_COUNT; i++)
    {
        if (k_mode_cycle[i] == m)
        {
            return i;
        }
    }
    return 0;
}

static int index_of_fan(daikin_fan_t f)
{
    for (int i = 0; i < FAN_COUNT; i++)
    {
        if (k_fan_cycle[i] == f)
        {
            return i;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Widgets                                                             */

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_pad_left(l, 6, 0);
    lv_obj_set_style_pad_right(l, 6, 0);
    lv_obj_set_style_pad_top(l, 2, 0);
    lv_obj_set_style_pad_bottom(l, 2, 0);
    lv_obj_set_style_radius(l, 6, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* Square keycap: the outer bottom border and an inset stroke make a
 * double bottom edge. Keep the legend above that edge, on the key face. */
static void make_keycap(lv_obj_t *parent, const char *text, const lv_font_t *font,
                        int x, int y, int size)
{
    lv_obj_t *cap = lv_obj_create(parent);
    lv_obj_set_pos(cap, x, y);
    lv_obj_set_size(cap, size, size);
    lv_obj_set_style_bg_color(cap, lv_color_hex(0x181818), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cap, 1, 0);
    lv_obj_set_style_border_color(cap, lv_color_hex(0x999999), 0);
    lv_obj_set_style_radius(cap, 0, 0);
    lv_obj_set_style_pad_all(cap, 0, 0);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *edge = lv_obj_create(cap);
    lv_obj_set_pos(edge, 0, size - 6);
    lv_obj_set_size(edge, size - 2, 1);
    lv_obj_set_style_bg_color(edge, lv_color_hex(0x999999), 0);
    lv_obj_set_style_bg_opa(edge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(edge, 0, 0);
    lv_obj_set_style_radius(edge, 0, 0);
    lv_obj_set_style_pad_all(edge, 0, 0);
    lv_obj_clear_flag(edge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *legend = make_label(cap, text, font, COLOR_SELECT, 0, 0);
    lv_obj_set_style_pad_all(legend, 0, 0);
    lv_obj_set_width(legend, size - 2);
    lv_obj_set_style_text_align(legend, LV_TEXT_ALIGN_CENTER, 0);
}

static lv_obj_t *make_key_field(lv_obj_t *parent, const char *key, const char *text,
                               const lv_font_t *font, uint32_t color, int x, int y)
{
    make_keycap(parent, key, &lv_font_montserrat_20, x, y, 32);
    return make_label(parent, text, font, color, x + 36, y);
}

static void style_field(lv_obj_t *l, bool selected, bool on)
{
    if (selected)
    {
        lv_obj_set_style_bg_color(l, lv_color_hex(COLOR_SELECT), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
    }
    else
    {
        lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(on ? COLOR_TEXT : COLOR_DIM), 0);
    }
}

static void build_key_guide(lv_obj_t *parent)
{
    /* Match the physical keypad, including its left navigation column. */
    static const char *const keys[20] = {
        LV_SYMBOL_UP, "Esc", LV_SYMBOL_HOME, LV_SYMBOL_ENVELOPE,
        LV_SYMBOL_NEW_LINE, "1", "2", "3",
        LV_SYMBOL_DOWN, "4", "5", "6",
        LV_SYMBOL_BACKSPACE, "7", "8", "9",
        LV_SYMBOL_STOP, "*", "0", "#",
    };
    static const char *const actions[20] = {
        "Prev field", "Key guide", "Temp field", "Send now",
        "Value +", "Mode", "Fan", "Powerful",
        "Next field", "Swing V", "Swing H", "Econo",
        "Value -", "Quiet", "Eye", "Comfort",
        "AC off", "Temp -0.5", "Power", "Temp +0.5",
    };

    s_help = lv_obj_create(parent);
    lv_obj_set_pos(s_help, 10, 48);
    lv_obj_set_size(s_help, 460, 324);
    lv_obj_set_style_bg_color(s_help, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_help, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_help, 0, 0);
    lv_obj_set_style_pad_all(s_help, 0, 0);
    lv_obj_clear_flag(s_help, LV_OBJ_FLAG_SCROLLABLE);
    make_label(s_help, "KEYPAD HELP", &lv_font_montserrat_20,
               COLOR_ACCENT, 8, 2);

    for (int i = 0; i < 20; i++)
    {
        lv_obj_t *cell = lv_obj_create(s_help);
        lv_obj_set_pos(cell, 8 + (i % 4) * 112, 38 + (i / 4) * 56);
        lv_obj_set_size(cell, 108, 54);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        make_keycap(cell, keys[i], i == 1 ? &lv_font_montserrat_16 : &lv_font_montserrat_20,
                    38, 0, 32);
        lv_obj_t *action = make_label(cell, actions[i], &lv_font_montserrat_16,
                                      COLOR_TEXT, 0, 32);
        lv_obj_set_width(action, 108);
        lv_obj_set_style_text_align(action, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_add_flag(s_help, LV_OBJ_FLAG_HIDDEN);
}

static void build_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    static lv_style_t style;

    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_hex(COLOR_BG));
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_border_width(&style, 0);
    lv_obj_add_style(scr, &style, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_title = make_label(scr, "DAIKIN ARC466A41", &lv_font_montserrat_20, COLOR_ACCENT, 14, 14);
    s_clock = make_label(scr, "clock unset", &lv_font_montserrat_20, COLOR_DIM, 300, 14);
    lv_obj_set_width(s_clock, 166);
    lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_RIGHT, 0);

    /* Left column: power, temperature, mode. */
    s_fields[F_POWER] = make_key_field(scr, "0", "OFF", &lv_font_montserrat_30, COLOR_TEXT, 14, 62);
    s_fields[F_TEMP] = make_label(scr, "25.0", &lv_font_montserrat_48, COLOR_TEXT, 14, 118);
    s_unit = make_label(scr, "\xC2\xB0" "C", &lv_font_montserrat_30, COLOR_TEXT, 150, 126);
    make_keycap(scr, "*", &lv_font_montserrat_16, 14, 178, 24);
    make_label(scr, "-0.5", &lv_font_montserrat_16, COLOR_ACCENT, 38, 178);
    make_keycap(scr, "#", &lv_font_montserrat_16, 106, 178, 24);
    make_label(scr, "+0.5", &lv_font_montserrat_16, COLOR_ACCENT, 130, 178);
    s_fields[F_MODE] = make_key_field(scr, "1", "COOL", &lv_font_montserrat_36, COLOR_TEXT, 14, 206);

    /* Right column: fan, swing, feature flags. */
    s_fields[F_FAN] = make_key_field(scr, "2", "FAN AUTO", &lv_font_montserrat_24, COLOR_TEXT, 250, 66);
    s_fields[F_SWING_V] = make_key_field(scr, "4", "SWING V", &lv_font_montserrat_24, COLOR_DIM, 250, 110);
    s_fields[F_SWING_H] = make_key_field(scr, "5", "SWING H", &lv_font_montserrat_24, COLOR_DIM, 250, 150);
    s_fields[F_POWERFUL] = make_key_field(scr, "3", "POWERFUL", &lv_font_montserrat_24, COLOR_DIM, 250, 190);
    s_fields[F_ECONO] = make_key_field(scr, "6", "ECONO", &lv_font_montserrat_24, COLOR_DIM, 250, 230);
    s_fields[F_QUIET] = make_key_field(scr, "7", "QUIET", &lv_font_montserrat_24, COLOR_DIM, 250, 270);
    s_fields[F_SENSOR] = make_key_field(scr, "8", "EYE", &lv_font_montserrat_24, COLOR_DIM, 380, 270);
    s_fields[F_COMFORT] = make_key_field(scr, "9", "COMFORT", &lv_font_montserrat_24, COLOR_DIM, 250, 310);
    s_timer = make_label(scr, "", &lv_font_montserrat_20, COLOR_ACCENT, 14, 270);

    /* Room sensor (env_sensor.c), left column level with COMFORT. Widest
     * texts: "-40.0 C  100%RH" (degree sign, no space) 199 px in Montserrat
     * 24 and "room sensor: not found" (wider than "room  1100.0 hPa abs",
     * 168 px) in 16, so with padding they
     * end at x = 225, clear of the right column (x >= 250) and of the
     * status line (y >= 380). */
    s_env = make_label(scr, "--.-" "\xC2\xB0" "C  --%RH", &lv_font_montserrat_24, COLOR_DIM, 14, 308);
    s_env_sub = make_label(scr, "room sensor: starting", &lv_font_montserrat_16, COLOR_DIM, 14, 342);

    s_status = make_label(scr, "", &lv_font_montserrat_24, COLOR_OK, 14, 380);
    s_hint = make_label(scr, "", &lv_font_montserrat_16, COLOR_DIM, 14, 424);
    build_key_guide(scr);
}

/* Why the IR LED may not light. Each text fits the 480 px status line in
 * Montserrat 24 (at most ~440 px, kerning included). */
static void show_ir_power(ir_power_t pwr)
{
    char msg[48];
    const char *text = msg;
    uint32_t color = COLOR_WARN;

    switch (pwr)
    {
    case IR_POWER_NO_KEYBOARD:
        text = "NO KEYBOARD: IR on PA36 only";
        break;
    case IR_POWER_EN_FAILED:
        text = "NO IR POWER: WIFI_EN low";
        color = COLOR_ERR;
        break;
    case IR_POWER_NO_BATTERY:
        snprintf(msg, sizeof(msg), "NO IR POWER: no battery (%d mV)", ir_power_vbat_mv());
        color = COLOR_ERR;
        break;
    case IR_POWER_LOW_BATTERY:
        snprintf(msg, sizeof(msg), "WEAK IR: battery low (%d mV)", ir_power_vbat_mv());
        break;
    default: /* IR_POWER_VBAT_UNKNOWN: "battery not measured" */
        snprintf(msg, sizeof(msg), "IR POWER ?: %s", ir_power_str(pwr));
        break;
    }
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
}

static void refresh(void)
{
    char buf[32];
    uint16_t minutes;
    uint8_t day;
    bool clock_set;
    daikin_state_t s;

    ac_ctrl_lock();
    s = *ac_ctrl_state();
    clock_set = ac_ctrl_clock_now(&minutes, &day);
    ac_ctrl_unlock();

    if (clock_set)
    {
        snprintf(buf, sizeof(buf), "%02u:%02u %s", minutes / 60, minutes % 60, daikin_day_name(day));
        lv_label_set_text(s_clock, buf);
        lv_obj_set_style_text_color(s_clock, lv_color_hex(COLOR_TEXT), 0);
    }
    else
    {
        lv_label_set_text(s_clock, "clock unset");
        lv_obj_set_style_text_color(s_clock, lv_color_hex(COLOR_DIM), 0);
    }

    lv_label_set_text(s_fields[F_POWER], s.power ? "ON" : "OFF");
    style_field(s_fields[F_POWER], s_field == F_POWER, s.power);
    if (s_field != F_POWER && !s.power)
    {
        lv_obj_set_style_text_color(s_fields[F_POWER], lv_color_hex(COLOR_ERR), 0);
    }

    if (s.mode == DAIKIN_MODE_FAN)
    {
        lv_label_set_text(s_fields[F_TEMP], "--.-");
    }
    else
    {
        snprintf(buf, sizeof(buf), "%u.%u", s.temp_half_deg / 2, (s.temp_half_deg % 2) * 5);
        lv_label_set_text(s_fields[F_TEMP], buf);
    }
    style_field(s_fields[F_TEMP], s_field == F_TEMP, s.power && s.mode != DAIKIN_MODE_FAN);
    lv_obj_set_style_text_color(s_unit, lv_color_hex(s.mode != DAIKIN_MODE_FAN ? COLOR_TEXT : COLOR_DIM), 0);

    lv_label_set_text(s_fields[F_MODE], daikin_mode_name(s.mode));
    style_field(s_fields[F_MODE], s_field == F_MODE, true);

    snprintf(buf, sizeof(buf), "FAN %s", daikin_fan_name(s.fan));
    lv_label_set_text(s_fields[F_FAN], buf);
    style_field(s_fields[F_FAN], s_field == F_FAN, true);

    style_field(s_fields[F_SWING_V], s_field == F_SWING_V, s.swing_v);
    style_field(s_fields[F_SWING_H], s_field == F_SWING_H, s.swing_h);
    style_field(s_fields[F_POWERFUL], s_field == F_POWERFUL, s.powerful);
    style_field(s_fields[F_ECONO], s_field == F_ECONO, s.econo);
    style_field(s_fields[F_QUIET], s_field == F_QUIET, s.quiet);
    style_field(s_fields[F_SENSOR], s_field == F_SENSOR, s.sensor);
    style_field(s_fields[F_COMFORT], s_field == F_COMFORT, s.comfort);

    if (s_help_visible)
    {
        lv_obj_remove_flag(s_help, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, "A / ESC close   HOME returns to temperature\n"
                                  "Hold: A send   B back   C power");
    }
    else
    {
        lv_obj_add_flag(s_help, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, keypad_available()
            ? "A help   * / # temp   0 power   MAIL send\n"
              "B +  C -   |   Aim RIGHT side at AC"
            : "A next  B +  C -    hold: A send  B back  C power\n"
              "IR LED: aim the RIGHT SIDE at the AC");
    }

    if (s.on_timer || s.off_timer)
    {
        char on_t[8] = "--:--", off_t[8] = "--:--";
        if (s.on_timer)
        {
            snprintf(on_t, sizeof(on_t), "%02u:%02u", s.on_timer_min / 60, s.on_timer_min % 60);
        }
        if (s.off_timer)
        {
            snprintf(off_t, sizeof(off_t), "%02u:%02u", s.off_timer_min / 60, s.off_timer_min % 60);
        }
        snprintf(buf, sizeof(buf), "ON %s  OFF %s", on_t, off_t);
        lv_label_set_text(s_timer, buf);
    }
    else
    {
        lv_label_set_text(s_timer, "");
    }

    if (flash_active())
    {
        /* flash still active: text set when the event fired */
    }
    else if (ir_power_battery_low())
    {
        /* ir_power.c switches the board off 30 s after this appears. */
        lv_label_set_text(s_status, "LOW BATTERY: plug in USB");
        lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_ERR), 0);
    }
    else if (ac_ctrl_pending())
    {
        lv_label_set_text(s_status, "sending...");
        lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_WARN), 0);
    }
    else if (!ac_ctrl_ir_ready())
    {
        lv_label_set_text(s_status, "IR transmitter unavailable");
        lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_ERR), 0);
    }
    else if (ir_power_state() != IR_POWER_OK)
    {
        show_ir_power(ir_power_state());
    }
    else
    {
        lv_label_set_text(s_status, ac_ctrl_auto_send() ? "" : "auto-send off");
        lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_DIM), 0);
    }
}

static void flash_status(const char *text, uint32_t color)
{
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    s_flash_until = rt_tick_get() + rt_tick_from_millisecond(1500);
}

/* After a frame went out on PA36: say which power bit it carried (taken
 * from the frame itself, which also covers "ac sendraw"), and do not claim
 * success when the LED has no supply. */
static void flash_sent(void)
{
    bool on = ac_ctrl_last_sent_power();

    switch (ir_power_state())
    {
    case IR_POWER_OK:
        flash_status(on ? "SENT: ON" : "SENT: OFF", COLOR_OK);
        break;
    case IR_POWER_LOW_BATTERY:
        flash_status(on ? "SENT: ON (battery low, weak IR)" : "SENT: OFF (battery low, weak IR)",
                     COLOR_WARN);
        break;
    case IR_POWER_VBAT_UNKNOWN:
        flash_status(on ? "SENT: ON (IR power not verified)" : "SENT: OFF (IR power not verified)",
                     COLOR_WARN);
        break;
    case IR_POWER_NO_KEYBOARD:
        /* No on-board LED; an external one on CON1 pin 14 still gets it. */
        flash_status(on ? "SENT: ON (PA36 only)" : "SENT: OFF (PA36 only)", COLOR_WARN);
        break;
    default:
        flash_status("NOT EMITTED: no IR power", COLOR_ERR);
        break;
    }
}

/* Room temperature / humidity / pressure. Touches the labels only when
 * their text changes (a new value, or the sample went stale). */
static void refresh_env(void)
{
    static char shown[2][32];
    char main_txt[32], sub_txt[32], t[12], p[12];
    env_reading_t r;

    env_sensor_get(&r);
    if (r.valid)
    {
        env_fmt_tenths(t, sizeof(t), r.temp_dc, false);
        env_fmt_tenths(p, sizeof(p), r.hpa_d, false);
        snprintf(main_txt, sizeof(main_txt), "%s\xC2\xB0" "C  %d%%RH", t, (r.rh_d + 5) / 10);
        snprintf(sub_txt, sizeof(sub_txt), "room  %s hPa abs", p); /* station pressure */
    }
    else
    {
        snprintf(main_txt, sizeof(main_txt), "--.-\xC2\xB0" "C  --%%RH");
        snprintf(sub_txt, sizeof(sub_txt), "room sensor: %s", env_status_str(r.status));
    }
    if (strcmp(main_txt, shown[0]) != 0)
    {
        lv_label_set_text(s_env, main_txt);
        lv_obj_set_style_text_color(s_env, lv_color_hex(r.valid ? COLOR_ACCENT : COLOR_DIM), 0);
        strcpy(shown[0], main_txt);
    }
    if (strcmp(sub_txt, shown[1]) != 0)
    {
        lv_label_set_text(s_env_sub, sub_txt);
        strcpy(shown[1], sub_txt);
    }
}

/* ------------------------------------------------------------------ */
/* Event handling (UI thread only)                                     */

static void adjust(int dir)
{
    daikin_state_t *s;
    bool changed = true;

    ac_ctrl_lock();
    s = ac_ctrl_state();
    switch (s_field)
    {
    case F_POWER:
        s->power = !s->power;
        break;
    case F_MODE:
        s->mode = k_mode_cycle[(index_of_mode(s->mode) + dir + MODE_COUNT) % MODE_COUNT];
        break;
    case F_TEMP:
    {
        int t = s->temp_half_deg + dir;
        if (t < UI_MIN_TEMP_HALF) t = UI_MIN_TEMP_HALF;
        if (t > UI_MAX_TEMP_HALF) t = UI_MAX_TEMP_HALF;
        changed = t != s->temp_half_deg;
        s->temp_half_deg = (uint8_t)t;
        break;
    }
    case F_FAN:
        s->fan = k_fan_cycle[(index_of_fan(s->fan) + dir + FAN_COUNT) % FAN_COUNT];
        break;
    case F_SWING_V:
        s->swing_v = !s->swing_v;
        break;
    case F_SWING_H:
        s->swing_h = !s->swing_h;
        break;
    case F_POWERFUL:
        s->powerful = !s->powerful;
        if (s->powerful) s->econo = s->quiet = false;
        break;
    case F_ECONO:
        s->econo = !s->econo;
        if (s->econo) s->powerful = false;
        break;
    case F_QUIET:
        s->quiet = !s->quiet;
        if (s->quiet) s->powerful = false;
        break;
    case F_SENSOR:
        s->sensor = !s->sensor;
        break;
    case F_COMFORT:
        s->comfort = !s->comfort;
        break;
    default:
        changed = false;
        break;
    }
    ac_ctrl_unlock();
    if (changed) ac_ctrl_mark_changed();
}

static void shortcut(field_t field, int dir)
{
    s_field = field;
    adjust(dir);
}

static void handle_event(ui_event_t evt)
{
    if (evt != UI_EVT_TOGGLE_HELP) s_help_visible = false;
    switch (evt)
    {
    case UI_EVT_HOME:
        s_field = F_TEMP;
        break;
    case UI_EVT_TOGGLE_HELP:
        s_help_visible = !s_help_visible;
        break;
    case UI_EVT_NEXT_FIELD:
        s_field = (field_t)((s_field + 1) % F_COUNT);
        break;
    case UI_EVT_PREV_FIELD:
        s_field = (field_t)((s_field + F_COUNT - 1) % F_COUNT);
        break;
    case UI_EVT_VALUE_UP:
        adjust(+1);
        break;
    case UI_EVT_VALUE_DOWN:
        adjust(-1);
        break;
    case UI_EVT_TOGGLE_POWER:
        shortcut(F_POWER, +1);
        break;
    case UI_EVT_POWER_OFF:
    {
        s_field = F_POWER;
        ac_ctrl_lock();
        ac_ctrl_state()->power = false;
        ac_ctrl_unlock();
        ac_ctrl_mark_changed();
        break;
    }
    case UI_EVT_TEMP_UP:
        shortcut(F_TEMP, +1);
        break;
    case UI_EVT_TEMP_DOWN:
        shortcut(F_TEMP, -1);
        break;
    case UI_EVT_CYCLE_MODE:
        shortcut(F_MODE, +1);
        break;
    case UI_EVT_CYCLE_FAN:
        shortcut(F_FAN, +1);
        break;
    case UI_EVT_TOGGLE_SWING_V:
        shortcut(F_SWING_V, +1);
        break;
    case UI_EVT_TOGGLE_SWING_H:
        shortcut(F_SWING_H, +1);
        break;
    case UI_EVT_TOGGLE_POWERFUL:
        shortcut(F_POWERFUL, +1);
        break;
    case UI_EVT_TOGGLE_ECONO:
        shortcut(F_ECONO, +1);
        break;
    case UI_EVT_TOGGLE_QUIET:
        shortcut(F_QUIET, +1);
        break;
    case UI_EVT_TOGGLE_SENSOR:
        shortcut(F_SENSOR, +1);
        break;
    case UI_EVT_TOGGLE_COMFORT:
        shortcut(F_COMFORT, +1);
        break;
    case UI_EVT_SEND:
        if (ac_ctrl_send() == 0)
        {
            flash_sent();
        }
        else
        {
            flash_status("IR SEND FAILED", COLOR_ERR);
        }
        s_last_send_count = ac_ctrl_send_count();
        break;
    default:
        break;
    }
    s_dirty = true;
}

void ui_post(ui_event_t evt)
{
    rt_mb_send(&s_mb, (rt_ubase_t)evt);
}

void ui_invalidate(void)
{
    s_dirty = true;
}

static void ui_thread(void *param)
{
    (void)param;
    while (1)
    {
        rt_ubase_t evt;
        uint32_t ms;
        uint16_t minutes;
        uint8_t day;

        while (rt_mb_recv(&s_mb, &evt, 0) == RT_EOK)
        {
            handle_event((ui_event_t)evt);
        }

        /* Auto-send completed in the worker thread? */
        if (ac_ctrl_send_count() != s_last_send_count)
        {
            s_last_send_count = ac_ctrl_send_count();
            flash_sent();
            s_dirty = true;
        }
        if (s_flash_until && !flash_active())
        {
            s_flash_until = 0;
            s_dirty = true;
        }
        if (ac_ctrl_pending() != s_last_pending)
        {
            s_last_pending = ac_ctrl_pending();
            s_dirty = true;
        }
        minutes = ac_ctrl_clock_now(&minutes, &day) ? minutes : 0xFFFE;
        if (minutes != s_last_minute)
        {
            s_last_minute = minutes;
            s_dirty = true;
        }
        /* New room-sensor data, plus a once-a-second look so that a sample
         * that went stale is blanked. */
        if (env_sensor_seq() != s_env_seq ||
            rt_tick_get() - s_env_tick >= rt_tick_from_millisecond(1000))
        {
            s_env_seq = env_sensor_seq();
            s_env_tick = rt_tick_get();
            refresh_env();
        }

        if (s_dirty)
        {
            s_dirty = false;
            refresh();
        }
        ms = lv_task_handler();
        if (ms < 5) ms = 5;
        if (ms > 40) ms = 40;
        rt_thread_mdelay(ms);
    }
}

int ui_init(void)
{
    rt_err_t ret;

    rt_mb_init(&s_mb, "ui", s_mb_pool, sizeof(s_mb_pool) / sizeof(rt_ubase_t), RT_IPC_FLAG_FIFO);

    ret = littlevgl2rtt_init("lcd");
    if (ret != RT_EOK)
    {
        LOG_E("littlevgl2rtt_init failed: %d", ret);
        return -1;
    }
    build_screen();
    refresh();
    refresh_env();
    return 0;
}

void ui_start(void)
{
    rt_thread_t th = rt_thread_create("ui", ui_thread, RT_NULL, 8 * 1024, 20, 10);
    if (th != RT_NULL)
    {
        rt_thread_startup(th);
    }
}
