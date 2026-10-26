#include "ac_ctrl.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef RT_USING_DFS
#include <dfs_posix.h>
#endif

#include "env_sensor.h"
#include "ir_power.h"
#include "ir_tx.h"

#define LOG_TAG "ac"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define PERSIST_PATH "/daikin_remote.cfg"
#define PERSIST_VERSION 1
#define PERSIST_DELAY_MS 3000
#define RTC_VALID_YEAR 2024
#define FRAME_POWER_BYTE 21 /* section 3 [mode/power/timers]: bit 0 = power */

typedef struct
{
    uint8_t version;
    uint8_t auto_send;
    daikin_state_t state;
} persisted_t;

static struct rt_mutex s_lock;
static daikin_state_t s_state;
static bool s_auto_send = true;
static bool s_dirty;
static bool s_persist_dirty;
static rt_tick_t s_dirty_tick;
static uint8_t s_last_frame[DAIKIN_FRAME_BYTES];
static uint32_t s_send_count;
static uint32_t s_timings[DAIKIN_TIMING_COUNT];

/* ------------------------------------------------------------------ */
/* Persistence                                                         */

static void persist_save(void)
{
#ifdef RT_USING_DFS
    persisted_t p;
    int fd;

    memset(&p, 0, sizeof(p));
    p.version = PERSIST_VERSION;
    p.auto_send = s_auto_send;
    p.state = s_state;

    fd = open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        LOG_W("cannot write %s", PERSIST_PATH);
        return;
    }
    write(fd, &p, sizeof(p));
    close(fd);
#endif
    s_persist_dirty = false;
}

static void persist_load(void)
{
#ifdef RT_USING_DFS
    persisted_t p;
    int fd = open(PERSIST_PATH, O_RDONLY, 0);
    if (fd < 0)
    {
        return;
    }
    if (read(fd, &p, sizeof(p)) == (int)sizeof(p) && p.version == PERSIST_VERSION)
    {
        s_state = p.state;
        s_auto_send = p.auto_send;
        /* Timers and the clock are session-bound. */
        s_state.on_timer = false;
        s_state.off_timer = false;
        s_state.clock_min = 0;
        s_state.clock_day = 0;
        daikin_sanitize(&s_state);
        LOG_I("restored state from %s", PERSIST_PATH);
    }
    close(fd);
#endif
}

/* ------------------------------------------------------------------ */
/* Clock                                                               */

bool ac_ctrl_clock_now(uint16_t *minutes, uint8_t *day)
{
    time_t now = time(RT_NULL);
    struct tm tm_now;
    struct tm *p = localtime(&now);

    if (p == RT_NULL)
    {
        return false;
    }
    tm_now = *p;
    *minutes = (uint16_t)(tm_now.tm_hour * 60 + tm_now.tm_min);
    *day = (uint8_t)(tm_now.tm_wday + 1); /* tm_wday 0 = Sunday -> 1 */
    return (tm_now.tm_year + 1900) >= RTC_VALID_YEAR;
}

int ac_ctrl_set_clock(uint16_t minutes, uint8_t day)
{
#ifdef RT_USING_RTC
    time_t now = time(RT_NULL);
    struct tm tm_new;
    struct tm *p = localtime(&now);
    rt_device_t rtc;

    if (p == RT_NULL)
    {
        return -1;
    }
    tm_new = *p;
    if (tm_new.tm_year + 1900 < RTC_VALID_YEAR)
    {
        /* Never set: start from Sunday 2026-01-04 so weekdays are sane. */
        tm_new.tm_year = 2026 - 1900;
        tm_new.tm_mon = 0;
        tm_new.tm_mday = 4;
    }
    tm_new.tm_hour = minutes / 60;
    tm_new.tm_min = minutes % 60;
    tm_new.tm_sec = 0;
    now = mktime(&tm_new);
    if (day >= 1 && day <= 7)
    {
        /* Move forward to the next date with the requested weekday. */
        int want = day - 1;
        int have = localtime(&now)->tm_wday;
        int delta = (want - have + 7) % 7;
        now += (time_t)delta * 24 * 60 * 60;
    }
    rtc = rt_device_find("rtc");
    if (rtc == RT_NULL)
    {
        return -2;
    }
    rt_device_control(rtc, RT_DEVICE_CTRL_RTC_SET_TIME, &now);
    return 0;
#else
    (void)minutes;
    (void)day;
    return -3;
#endif
}

/* ------------------------------------------------------------------ */
/* State                                                               */

void ac_ctrl_lock(void)
{
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
}

void ac_ctrl_unlock(void)
{
    rt_mutex_release(&s_lock);
}

daikin_state_t *ac_ctrl_state(void)
{
    return &s_state;
}

void ac_ctrl_mark_changed(void)
{
    ac_ctrl_lock();
    daikin_sanitize(&s_state);
    s_dirty = true;
    s_persist_dirty = true;
    s_dirty_tick = rt_tick_get();
    ac_ctrl_unlock();
}

void ac_ctrl_build_frame(uint8_t out[DAIKIN_FRAME_BYTES])
{
    uint16_t minutes = 0;
    uint8_t day = 0;

    ac_ctrl_lock();
    if (ac_ctrl_clock_now(&minutes, &day))
    {
        s_state.clock_min = minutes;
        s_state.clock_day = day;
    }
    else
    {
        s_state.clock_min = 0;
        s_state.clock_day = 0;
    }
    daikin_sanitize(&s_state);
    daikin_encode(&s_state, &DAIKIN_PROFILE_ARC466A41, out);
    ac_ctrl_unlock();
}

int ac_ctrl_send_raw(const uint8_t frame[DAIKIN_FRAME_BYTES])
{
    size_t n;
    int ret;

    ac_ctrl_lock();
    n = daikin_to_timings(frame, &DAIKIN_TIMING_MEASURED, s_timings, DAIKIN_TIMING_COUNT);
    if (n == 0)
    {
        ac_ctrl_unlock();
        return -1;
    }
    ret = ir_tx_send(s_timings, n);
    if (ret == 0)
    {
        /* Count only frames that were actually clocked out on PA36. */
        memcpy(s_last_frame, frame, DAIKIN_FRAME_BYTES);
        s_send_count++;
    }
    ac_ctrl_unlock();
    return ret;
}

int ac_ctrl_send(void)
{
    uint8_t frame[DAIKIN_FRAME_BYTES];
    char hex[128];
    ir_power_t pwr;
    int ret;

    /* Battery / WIFI_3V3 check first: it sleeps, so never under the lock. */
    pwr = ir_power_check();
    ac_ctrl_build_frame(frame);
    ac_ctrl_lock();
    s_dirty = false;
    ac_ctrl_unlock();
    ret = ac_ctrl_send_raw(frame);
    ac_ctrl_frame_hex(frame, hex, sizeof(hex));
    if (ret != 0)
    {
        rt_kprintf("TX FAILED (IR transmitter not ready): ");
    }
    else if (pwr == IR_POWER_OK)
    {
        rt_kprintf("TX ok: ");
    }
    else
    {
        rt_kprintf("TX on PA36 but IR LED power: %s (battery %d mV): ", ir_power_str(pwr),
                   ir_power_vbat_mv());
    }
    ac_console_puts(hex);
    rt_kprintf("\n");
    if (s_persist_dirty)
    {
        persist_save();
    }
    return ret;
}

bool ac_ctrl_pending(void)
{
    return s_dirty && s_auto_send;
}

bool ac_ctrl_auto_send(void)
{
    return s_auto_send;
}

void ac_ctrl_set_auto_send(bool on)
{
    s_auto_send = on;
    s_persist_dirty = true;
    s_dirty_tick = rt_tick_get();
}

uint32_t ac_ctrl_send_count(void)
{
    return s_send_count;
}

bool ac_ctrl_last_sent_power(void)
{
    bool on;

    ac_ctrl_lock();
    on = (s_last_frame[FRAME_POWER_BYTE] & 0x01) != 0;
    ac_ctrl_unlock();
    return on;
}

bool ac_ctrl_ir_ready(void)
{
    return ir_tx_ready();
}

void ac_ctrl_reset_defaults(void)
{
    ac_ctrl_lock();
    daikin_state_init(&s_state);
    ac_ctrl_unlock();
    ac_ctrl_mark_changed();
    /* Reset only the remote's local state (persisted later); do not
     * auto-transmit the default OFF frame to the unit. */
    ac_ctrl_lock();
    s_dirty = false;
    ac_ctrl_unlock();
}

/* ------------------------------------------------------------------ */
/* Formatting                                                          */

static const char *on_off(bool v)
{
    return v ? "on" : "off";
}

static const char *bool_str(bool v)
{
    return v ? "true" : "false";
}

static void fmt_time(uint16_t minutes, char *buf, size_t len)
{
    snprintf(buf, len, "%02u:%02u", minutes / 60, minutes % 60);
}

void ac_console_puts(const char *s)
{
    char piece[97];
    size_t n = strlen(s);
    size_t pos = 0;

    while (pos < n)
    {
        size_t take = n - pos;
        if (take > sizeof(piece) - 1)
        {
            take = sizeof(piece) - 1;
        }
        memcpy(piece, s + pos, take);
        piece[take] = '\0';
        rt_kprintf("%s", piece);
        pos += take;
    }
}

/* Room-sensor values as text, "--" (null in JSON) unless valid. */
static void env_values(const env_reading_t *r, const char *none, char t[12], char rh[12], char p[12])
{
    if (r->valid)
    {
        env_fmt_tenths(t, 12, r->temp_dc, false);
        env_fmt_tenths(rh, 12, r->rh_d, false);
        env_fmt_tenths(p, 12, r->hpa_d, false);
    }
    else
    {
        snprintf(t, 12, "%s", none);
        snprintf(rh, 12, "%s", none);
        snprintf(p, 12, "%s", none);
    }
}

void ac_ctrl_frame_hex(const uint8_t frame[DAIKIN_FRAME_BYTES], char *buf, size_t len)
{
    size_t pos = 0;
    for (size_t i = 0; i < DAIKIN_FRAME_BYTES && pos + 6 < len; i++)
    {
        pos += (size_t)snprintf(buf + pos, len - pos, "%02X%s", frame[i],
                                (i == 7 || i == 15) ? " | " : (i + 1 < DAIKIN_FRAME_BYTES ? " " : ""));
    }
}

void ac_ctrl_describe(char *buf, size_t len)
{
    char on_t[8], off_t[8], clk[8], env_t[12], env_rh[12], env_p[12];
    uint16_t minutes;
    uint8_t day;
    bool clock_set;
    env_reading_t env;

    env_sensor_get(&env);
    env_values(&env, "--", env_t, env_rh, env_p);
    ac_ctrl_lock();
    fmt_time(s_state.on_timer_min, on_t, sizeof(on_t));
    fmt_time(s_state.off_timer_min, off_t, sizeof(off_t));
    clock_set = ac_ctrl_clock_now(&minutes, &day);
    fmt_time(minutes, clk, sizeof(clk));
    snprintf(buf, len,
             "power=%s mode=%s temp=%u.%u fan=%s swingV=%s swingH=%s powerful=%s econo=%s "
             "quiet=%s sensor=%s comfort=%s onTimer=%s offTimer=%s clock=%s%s%s autosend=%s "
             "ir=%s vbat=%dmV charger=%s env=%s tempC=%s rh=%s hPa=%s",
             on_off(s_state.power), daikin_mode_name(s_state.mode), s_state.temp_half_deg / 2,
             (s_state.temp_half_deg % 2) * 5, daikin_fan_name(s_state.fan),
             on_off(s_state.swing_v), on_off(s_state.swing_h), on_off(s_state.powerful),
             on_off(s_state.econo), on_off(s_state.quiet), on_off(s_state.sensor),
             on_off(s_state.comfort), s_state.on_timer ? on_t : "off",
             s_state.off_timer ? off_t : "off", clock_set ? clk : "unset",
             clock_set ? " " : "", clock_set ? daikin_day_name(day) : "", on_off(s_auto_send),
             ir_power_str(ir_power_state()), ir_power_vbat_mv(), ir_power_charger_str(),
             env_status_str(env.status), env_t, env_rh, env_p);
    ac_ctrl_unlock();
}

void ac_ctrl_json(char *buf, size_t len)
{
    char on_t[8], off_t[8], clk[8], env_t[12], env_rh[12], env_p[12], env_age[12];
    uint16_t minutes;
    uint8_t day;
    bool clock_set;
    env_reading_t env;

    env_sensor_get(&env);
    env_values(&env, "null", env_t, env_rh, env_p);
    if (env.age_s >= 0)
    {
        snprintf(env_age, sizeof(env_age), "%ld", (long)env.age_s);
    }
    else
    {
        snprintf(env_age, sizeof(env_age), "null");
    }
    ac_ctrl_lock();
    fmt_time(s_state.on_timer_min, on_t, sizeof(on_t));
    fmt_time(s_state.off_timer_min, off_t, sizeof(off_t));
    clock_set = ac_ctrl_clock_now(&minutes, &day);
    fmt_time(minutes, clk, sizeof(clk));
    snprintf(buf, len,
             "{\"power\":%s,\"mode\":\"%s\",\"temp\":%u.%u,\"fan\":\"%s\",\"swingV\":%s,"
             "\"swingH\":%s,\"powerful\":%s,\"econo\":%s,\"quiet\":%s,\"sensor\":%s,"
             "\"comfort\":%s,\"onTimer\":%s%s%s,\"offTimer\":%s%s%s,\"clock\":%s%s%s,"
             "\"day\":%u,\"autosend\":%s,\"sends\":%u,\"irPower\":\"%s\",\"vbatMv\":%d,"
             "\"charger\":\"%s\",\"tempC\":%s,\"rh\":%s,\"hPa\":%s,\"envStatus\":\"%s\","
             "\"envAgeS\":%s}",
             bool_str(s_state.power), daikin_mode_name(s_state.mode), s_state.temp_half_deg / 2,
             (s_state.temp_half_deg % 2) * 5, daikin_fan_name(s_state.fan),
             bool_str(s_state.swing_v), bool_str(s_state.swing_h), bool_str(s_state.powerful),
             bool_str(s_state.econo), bool_str(s_state.quiet), bool_str(s_state.sensor),
             bool_str(s_state.comfort),
             s_state.on_timer ? "\"" : "", s_state.on_timer ? on_t : "null", s_state.on_timer ? "\"" : "",
             s_state.off_timer ? "\"" : "", s_state.off_timer ? off_t : "null", s_state.off_timer ? "\"" : "",
             clock_set ? "\"" : "", clock_set ? clk : "null", clock_set ? "\"" : "",
             clock_set ? day : 0, bool_str(s_auto_send), (unsigned)s_send_count,
             ir_power_str(ir_power_state()), ir_power_vbat_mv(), ir_power_charger_str(), env_t,
             env_rh, env_p, env_status_str(env.status), env_age);
    ac_ctrl_unlock();
}

/* ------------------------------------------------------------------ */
/* Worker thread: auto-send and deferred persistence                   */

static void ac_worker(void *param)
{
    (void)param;
    while (1)
    {
        rt_tick_t elapsed;

        ir_power_service(); /* keep the charger watchdog from power-cycling us */
        elapsed = rt_tick_get() - s_dirty_tick;
        if (s_dirty && s_auto_send && elapsed >= rt_tick_from_millisecond(AC_AUTO_SEND_DELAY_MS))
        {
            ac_ctrl_send();
        }
        else if (s_persist_dirty && elapsed >= rt_tick_from_millisecond(PERSIST_DELAY_MS))
        {
            ac_ctrl_lock();
            persist_save();
            ac_ctrl_unlock();
        }
        rt_thread_mdelay(50);
    }
}

void ac_ctrl_init(void)
{
    rt_thread_t th;

    rt_mutex_init(&s_lock, "ac", RT_IPC_FLAG_PRIO);
    daikin_state_init(&s_state);
    persist_load();

    th = rt_thread_create("ac_send", ac_worker, RT_NULL, 4096, 12, 10);
    if (th != RT_NULL)
    {
        rt_thread_startup(th);
    }
}
