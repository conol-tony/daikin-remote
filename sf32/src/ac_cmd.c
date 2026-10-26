/*
 * "ac" shell command for the RT-Thread msh console (1000000 baud on the
 * USB-C port). Type "ac help" for the grammar.
 */
#include <rtthread.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ac_ctrl.h"
#include "env_sensor.h"
#include "ir_power.h"
#include "ui.h"

/* What the ARC466A41 itself can send, as on the buttons (ui.c): set-point
 * 18.0 - 32.0 C, and no AUTO or HEAT on this cooling-only remote unless
 * REMOTE_HEAT_PUMP is defined. */
#define CMD_MIN_TEMP_HALF 36
#define CMD_MAX_TEMP_HALF 64
#ifdef REMOTE_HEAT_PUMP
#define MODE_CHOICES "auto|dry|cool|heat|fan"
#else
#define MODE_CHOICES "dry|cool|fan"
#endif

static void lower(char *s)
{
    for (; *s; s++)
    {
        *s = (char)tolower((unsigned char)*s);
    }
}

static int parse_on_off(const char *t, bool *out)
{
    if (!strcmp(t, "on") || !strcmp(t, "1") || !strcmp(t, "true") || !strcmp(t, "yes"))
    {
        *out = true;
        return 1;
    }
    if (!strcmp(t, "off") || !strcmp(t, "0") || !strcmp(t, "false") || !strcmp(t, "no"))
    {
        *out = false;
        return 1;
    }
    return 0;
}

static int parse_hhmm(const char *t, uint16_t *minutes)
{
    int h = 0, m = 0;
    if (sscanf(t, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59)
    {
        return 0;
    }
    *minutes = (uint16_t)(h * 60 + m);
    return 1;
}

static int parse_day(const char *t, uint8_t *day)
{
    static const char *const names[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
    for (uint8_t i = 0; i < 7; i++)
    {
        if (!strncmp(t, names[i], 3))
        {
            *day = (uint8_t)(i + 1);
            return 1;
        }
    }
    if (t[0] >= '0' && t[0] <= '7' && t[1] == '\0')
    {
        *day = (uint8_t)(t[0] - '0');
        return 1;
    }
    return 0;
}

static int parse_temp(const char *t, uint8_t *half)
{
    char *end = NULL;
    double v = strtod(t, &end);
    if (end == t)
    {
        return 0;
    }
    if (*end == ',' && end[1] >= '0' && end[1] <= '9')
    {
        v += (end[1] - '0') / 10.0;
    }
    if (v * 2 < CMD_MIN_TEMP_HALF || v * 2 > CMD_MAX_TEMP_HALF)
    {
        return 0;
    }
    *half = (uint8_t)(int)(v * 2 + 0.5);
    return 1;
}

static uint8_t clamp_temp(int half)
{
    if (half < CMD_MIN_TEMP_HALF)
    {
        return CMD_MIN_TEMP_HALF;
    }
    if (half > CMD_MAX_TEMP_HALF)
    {
        return CMD_MAX_TEMP_HALF;
    }
    return (uint8_t)half;
}

static int mode_supported(daikin_mode_t m)
{
#ifdef REMOTE_HEAT_PUMP
    (void)m;
    return 1;
#else
    return m == DAIKIN_MODE_DRY || m == DAIKIN_MODE_COOL || m == DAIKIN_MODE_FAN;
#endif
}

static int parse_hex_frame(int argc, char **argv, uint8_t out[DAIKIN_FRAME_BYTES])
{
    char joined[2 * DAIKIN_FRAME_BYTES + 1];
    size_t pos = 0;
    for (int i = 0; i < argc; i++)
    {
        size_t l = strlen(argv[i]);
        if (pos + l > 2 * DAIKIN_FRAME_BYTES)
        {
            return 0;
        }
        memcpy(joined + pos, argv[i], l);
        pos += l;
    }
    if (pos != 2 * DAIKIN_FRAME_BYTES)
    {
        return 0;
    }
    joined[pos] = '\0';
    for (size_t i = 0; i < DAIKIN_FRAME_BYTES; i++)
    {
        char b[3] = {joined[2 * i], joined[2 * i + 1], '\0'};
        char *end = NULL;
        long v = strtol(b, &end, 16);
        if (*end != '\0')
        {
            return 0;
        }
        out[i] = (uint8_t)v;
    }
    return 1;
}

static void print_status(const char *prefix)
{
    char buf[320];
    ac_ctrl_describe(buf, sizeof(buf));
    rt_kprintf("%s", prefix);
    ac_console_puts(buf);
    rt_kprintf("\n");
}

/* "-1.5", "+2", "-1,5", optionally followed by the unit ("c", "%", "%rh"),
 * rounded to tenths; env_sensor_set_offsets() checks the range. */
static int parse_tenths(const char *t, const char *unit, int *out)
{
    char b[16];
    char *end = NULL;
    double v;
    size_t n = strlen(t);

    if (n == 0 || n >= sizeof(b))
    {
        return 0;
    }
    memcpy(b, t, n + 1);
    for (char *c = b; *c; c++)
    {
        if (*c == ',')
        {
            *c = '.';
        }
    }
    v = strtod(b, &end);
    if (end == b || (*end != '\0' && strcmp(end, unit) != 0 && strcmp(end, "%rh") != 0))
    {
        return 0;
    }
    if (*end == '%' && unit[0] != '%')
    {
        return 0; /* "%rh" given for the temperature */
    }
    v *= 10.0;
    if (!(v > -1000.0 && v < 1000.0))
    {
        return 0;
    }
    *out = (int)(v + (v < 0 ? -0.5 : 0.5));
    return 1;
}

static void tenths_or_dash(char *buf, size_t len, int16_t v, bool plus)
{
    if (v == ENV_NO_VALUE)
    {
        snprintf(buf, len, "--");
    }
    else
    {
        env_fmt_tenths(buf, len, v, plus);
    }
}

/* The env line of "ac env" and of the "ac env offset" replies. Lines go
 * through ac_console_puts(): rt_kprintf() stops at 128 bytes. */
static void print_env_line(const char *prefix, const env_reading_t *r)
{
    char line[192], t[12], rh[12], p[12], dew[12], age[12], ot[12], orh[12];

    tenths_or_dash(t, sizeof(t), r->temp_dc, false);
    tenths_or_dash(rh, sizeof(rh), r->rh_d, false);
    tenths_or_dash(p, sizeof(p), r->hpa_d, false);
    tenths_or_dash(dew, sizeof(dew), r->dew_dc, false);
    env_fmt_tenths(ot, sizeof(ot), r->temp_off_dc, true);
    env_fmt_tenths(orh, sizeof(orh), r->rh_off_d, true);
    if (r->age_s >= 0)
    {
        snprintf(age, sizeof(age), "%lds", (long)r->age_s);
    }
    else
    {
        snprintf(age, sizeof(age), "--");
    }
    snprintf(line, sizeof(line), "%senv=%s tempC=%s rh=%s hPa=%s dewC=%s age=%s offsetC=%s offsetRh=%s\n",
             prefix, env_status_str(r->status), t, rh, p, dew, age, ot, orh);
    ac_console_puts(line);
}

static void print_env(void)
{
    env_reading_t r;
    char line[224], t[12], rh[12];

    env_sensor_get(&r);
    print_env_line("", &r);
    tenths_or_dash(t, sizeof(t), r.raw_temp_dc, false);
    tenths_or_dash(rh, sizeof(rh), r.raw_rh_d, false);
    snprintf(line, sizeof(line),
             "raw tempC=%s rh=%s (tempC + offsetC; rh recomputed for the same dew point, + offsetRh)\n",
             t, rh);
    ac_console_puts(line);
    if (r.status == ENV_OK)
    {
        snprintf(line, sizeof(line),
                 "BME280 at 0x%02x on i2c1: forced mode, oversampling x1, filter off, every 10 s; "
                 "%lu samples, %lu failed%s%s\n",
                 r.addr, (unsigned long)r.samples, (unsigned long)r.errors,
                 r.detail[0] ? "; note: " : "", r.detail);
    }
    else
    {
        snprintf(line, sizeof(line), "BME280 on i2c1: %s%s%s; %lu samples, %lu failed\n",
                 env_status_str(r.status), r.detail[0] ? ": " : "", r.detail,
                 (unsigned long)r.samples, (unsigned long)r.errors);
    }
    ac_console_puts(line);
}

/* ac env offset <tempC> [rh%] | ac env offset reset | ac env offset */
static void env_offset_cmd(int argc, char **argv)
{
    env_reading_t r;
    int t, rh, ret;

    env_sensor_get(&r);
    t = r.temp_off_dc;
    rh = r.rh_off_d;
    if (argc == 0)
    {
        print_env_line("OK env offset | ", &r);
        return;
    }
    if (argc == 1 && !strcmp(argv[0], "reset"))
    {
        t = 0;
        rh = 0;
    }
    else if (argc > 2 || !parse_tenths(argv[0], "c", &t) ||
             (argc == 2 && !parse_tenths(argv[1], "%", &rh)))
    {
        rt_kprintf("ERR env offset <tempC -10..10> [rh%% -20..20] | env offset reset\n");
        return;
    }
    ret = env_sensor_set_offsets(t, rh);
    if (ret == -1)
    {
        rt_kprintf("ERR env offset <tempC -10..10> [rh%% -20..20] | env offset reset\n");
        return;
    }
    ui_invalidate();
    env_sensor_get(&r);
    print_env_line(ret == 0 ? "OK env offset | " : "WARN env offset set, not saved | ", &r);
}

static void print_frame(void)
{
    uint8_t frame[DAIKIN_FRAME_BYTES];
    char hex[128];
    ac_ctrl_build_frame(frame);
    ac_ctrl_frame_hex(frame, hex, sizeof(hex));
    rt_kprintf("FRAME ");
    ac_console_puts(hex);
    rt_kprintf("\n");
}

static void help(void)
{
    /* In pieces: one rt_kprintf() prints at most 128 bytes. */
    ac_console_puts(
        "Daikin ARC466A41 emulator - usage: ac <command> [args]\n"
        "  on | off | power on|off|toggle\n"
        "  mode " MODE_CHOICES "\n"
        "  temp <18..32 in 0.5 steps> | temp + | temp -\n"
        "  fan auto|quiet|1|2|3|4|5\n"
        "  swing v on|off | swing h on|off | swing both|off\n"
        "  powerful on|off   econo on|off   quiet on|off (outdoor unit)\n"
        "  sensor on|off     comfort on|off   mold on|off   weekly on|off\n"
        "  timer on HH:MM | timer on off     timer off HH:MM | timer off off\n"
        "  clock HH:MM [sun..sat]            set the RTC stamped into frames\n"
        "  send                              transmit now\n"
        "  autosend on|off                   transmit automatically after changes\n"
        "  status | json | raw               show state / JSON / the 35-byte frame\n"
        "  sendraw <35 hex bytes>            transmit an arbitrary frame\n"
        "  reset                             defaults (off, cool, 25.0, fan auto)\n"
        "  env                               room temperature/humidity/pressure (BME280)\n"
        "  env offset <tempC> [rh%]          calibrate, e.g. env offset -1.5 (rh kept)\n"
        "  env offset reset                  remove the calibration offsets\n");
}

static int ac_cmd(int argc, char **argv)
{
    daikin_state_t *s;
    const char *cmd;
    bool flag = false;
    int changed = 0;

    if (argc < 2)
    {
        help();
        return 0;
    }
    for (int i = 1; i < argc; i++)
    {
        lower(argv[i]);
    }
    cmd = argv[1];

    /* --- commands that do not change the state --- */
    if (!strcmp(cmd, "help") || !strcmp(cmd, "?"))
    {
        help();
        return 0;
    }
    if (!strcmp(cmd, "status"))
    {
        print_status("");
        return 0;
    }
    if (!strcmp(cmd, "json"))
    {
        char buf[512];
        ac_ctrl_json(buf, sizeof(buf));
        ac_console_puts(buf);
        rt_kprintf("\n");
        return 0;
    }
    if (!strcmp(cmd, "raw"))
    {
        print_frame();
        return 0;
    }
    if (!strcmp(cmd, "env"))
    {
        if (argc >= 3 && !strcmp(argv[2], "offset"))
        {
            env_offset_cmd(argc - 3, argv + 3);
        }
        else if (argc == 2)
        {
            env_sensor_refresh(500); /* a fresh sample, or a re-probe */
            print_env();
        }
        else
        {
            rt_kprintf("ERR env | env offset <tempC> [rh%%] | env offset reset\n");
        }
        return 0;
    }
    if (!strcmp(cmd, "send"))
    {
        print_frame();
        if (ac_ctrl_send() == 0)
        {
            print_status(ir_power_state() == IR_POWER_OK ? "OK sent | "
                                                         : "WARN sent, IR LED power not ok | ");
            ui_invalidate();
        }
        else
        {
            rt_kprintf("ERR IR transmitter not ready\n");
        }
        return 0;
    }
    if (!strcmp(cmd, "sendraw"))
    {
        uint8_t frame[DAIKIN_FRAME_BYTES];
        if (!parse_hex_frame(argc - 2, argv + 2, frame))
        {
            rt_kprintf("ERR expected 35 hex bytes\n");
            return 0;
        }
        if (!daikin_valid_frame(frame))
        {
            rt_kprintf("WARN frame checksums/headers invalid, sending anyway\n");
        }
        ir_power_check();
        if (ac_ctrl_send_raw(frame) != 0)
        {
            rt_kprintf("ERR IR transmitter not ready\n");
        }
        else
        {
            rt_kprintf(ir_power_state() == IR_POWER_OK ? "OK raw frame sent\n"
                                                       : "WARN raw frame sent, IR LED power not ok\n");
            ui_invalidate();
        }
        return 0;
    }
    if (!strcmp(cmd, "autosend"))
    {
        if (argc < 3 || !parse_on_off(argv[2], &flag))
        {
            rt_kprintf("ERR autosend on|off\n");
            return 0;
        }
        ac_ctrl_set_auto_send(flag);
        print_status("OK autosend | ");
        return 0;
    }
    if (!strcmp(cmd, "clock"))
    {
        uint16_t minutes;
        uint8_t day = 0;
        if (argc < 3 || !parse_hhmm(argv[2], &minutes))
        {
            rt_kprintf("ERR clock HH:MM [sun..sat]\n");
            return 0;
        }
        if (argc >= 4 && !parse_day(argv[3], &day))
        {
            rt_kprintf("ERR day must be sun..sat\n");
            return 0;
        }
        if (ac_ctrl_set_clock(minutes, day) != 0)
        {
            rt_kprintf("ERR RTC not available\n");
            return 0;
        }
        ui_invalidate();
        print_status("OK clock | ");
        return 0;
    }
    if (!strcmp(cmd, "reset"))
    {
        ac_ctrl_reset_defaults();
        ui_invalidate();
        print_status("OK reset | ");
        return 0;
    }

    /* --- state-changing commands --- */
    ac_ctrl_lock();
    s = ac_ctrl_state();
    if (!strcmp(cmd, "on"))
    {
        s->power = true;
        changed = 1;
    }
    else if (!strcmp(cmd, "off"))
    {
        s->power = false;
        changed = 1;
    }
    else if (!strcmp(cmd, "power"))
    {
        if (argc >= 3 && !strcmp(argv[2], "toggle"))
        {
            s->power = !s->power;
            changed = 1;
        }
        else if (argc >= 3 && parse_on_off(argv[2], &s->power))
        {
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR power on|off|toggle\n");
        }
    }
    else if (!strcmp(cmd, "mode"))
    {
        daikin_mode_t mode;

        if (argc >= 3 && daikin_parse_mode(argv[2], &mode) && mode_supported(mode))
        {
            s->mode = mode;
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR mode " MODE_CHOICES "\n");
        }
    }
    else if (!strcmp(cmd, "temp"))
    {
        if (argc < 3)
        {
            rt_kprintf("ERR temp <value>|+|-\n");
        }
        else if (!strcmp(argv[2], "+") || !strcmp(argv[2], "up"))
        {
            s->temp_half_deg = clamp_temp(s->temp_half_deg + 1);
            changed = 1;
        }
        else if (!strcmp(argv[2], "-") || !strcmp(argv[2], "down"))
        {
            s->temp_half_deg = clamp_temp(s->temp_half_deg - 1);
            changed = 1;
        }
        else if (parse_temp(argv[2], &s->temp_half_deg))
        {
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR temp must be 18..32\n");
        }
    }
    else if (!strcmp(cmd, "fan"))
    {
        if (argc >= 3 && daikin_parse_fan(argv[2], &s->fan))
        {
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR fan auto|quiet|1..5\n");
        }
    }
    else if (!strcmp(cmd, "swing"))
    {
        if (argc >= 3 && !strcmp(argv[2], "both"))
        {
            s->swing_v = s->swing_h = true;
            changed = 1;
        }
        else if (argc >= 3 && !strcmp(argv[2], "off"))
        {
            s->swing_v = s->swing_h = false;
            changed = 1;
        }
        else if (argc >= 4 && (!strcmp(argv[2], "v") || !strcmp(argv[2], "vertical")) && parse_on_off(argv[3], &flag))
        {
            s->swing_v = flag;
            changed = 1;
        }
        else if (argc >= 4 && (!strcmp(argv[2], "h") || !strcmp(argv[2], "horizontal")) && parse_on_off(argv[3], &flag))
        {
            s->swing_h = flag;
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR swing v|h on|off, swing both, swing off\n");
        }
    }
    else if (!strcmp(cmd, "powerful") || !strcmp(cmd, "boost"))
    {
        if (argc >= 3 && parse_on_off(argv[2], &s->powerful))
        {
            if (s->powerful)
            {
                s->econo = false;
                s->quiet = false;
            }
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR powerful on|off\n");
        }
    }
    else if (!strcmp(cmd, "econo") || !strcmp(cmd, "eco"))
    {
        if (argc >= 3 && parse_on_off(argv[2], &s->econo))
        {
            if (s->econo)
            {
                s->powerful = false;
            }
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR econo on|off\n");
        }
    }
    else if (!strcmp(cmd, "quiet"))
    {
        if (argc >= 3 && parse_on_off(argv[2], &s->quiet))
        {
            if (s->quiet)
            {
                s->powerful = false;
            }
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR quiet on|off\n");
        }
    }
    else if (!strcmp(cmd, "sensor") || !strcmp(cmd, "eye"))
    {
        if (argc >= 3 && parse_on_off(argv[2], &s->sensor))
        {
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR sensor on|off\n");
        }
    }
    else if (!strcmp(cmd, "comfort"))
    {
        if (argc >= 3 && parse_on_off(argv[2], &s->comfort))
        {
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR comfort on|off\n");
        }
    }
    else if (!strcmp(cmd, "mold") || !strcmp(cmd, "mould"))
    {
        if (argc >= 3 && parse_on_off(argv[2], &s->mold))
        {
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR mold on|off\n");
        }
    }
    else if (!strcmp(cmd, "weekly"))
    {
        if (argc >= 3 && parse_on_off(argv[2], &flag))
        {
            s->weekly_timer_off = !flag;
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR weekly on|off\n");
        }
    }
    else if (!strcmp(cmd, "timer"))
    {
        bool *enable = NULL;
        uint16_t *minutes = NULL;
        if (argc >= 4 && !strcmp(argv[2], "on"))
        {
            enable = &s->on_timer;
            minutes = &s->on_timer_min;
        }
        else if (argc >= 4 && !strcmp(argv[2], "off"))
        {
            enable = &s->off_timer;
            minutes = &s->off_timer_min;
        }
        if (enable == NULL)
        {
            rt_kprintf("ERR timer on|off HH:MM|off\n");
        }
        else if (!strcmp(argv[3], "off") || !strcmp(argv[3], "cancel"))
        {
            *enable = false;
            changed = 1;
        }
        else if (parse_hhmm(argv[3], minutes))
        {
            *enable = true;
            changed = 1;
        }
        else
        {
            rt_kprintf("ERR time must be HH:MM\n");
        }
    }
    else
    {
        rt_kprintf("ERR unknown command, try 'ac help'\n");
    }
    ac_ctrl_unlock();

    if (changed)
    {
        ac_ctrl_mark_changed();
        ui_invalidate();
        print_status("OK | ");
    }
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ac_cmd, ac, Daikin ARC466A41 remote control - ac help);
