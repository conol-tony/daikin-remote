/* SPDX-License-Identifier: MIT AND BSD-3-Clause
 * The compensation routines are adapted from Bosch BME280 SensorAPI.
 * Copyright (c) 2020 Bosch Sensortec GmbH. All rights reserved.
 * See licenses/BSD-3-Clause-Bosch.txt and THIRD_PARTY_NOTICES.md.
 */
#include "env_sensor.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <rtthread.h>
#include <rtdevice.h>

#ifdef RT_USING_DFS
#include <dfs_posix.h>
#endif

#include "ir_power.h"

#define LOG_TAG "env"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* Keyboard board schematic, thermo-hygrometer block: U2 BME280 with VDD,
 * VDDIO and CSB on WIFI_3V3, marked "I2C Address:0x76" (SDO low). I2C1 is
 * shared with the charger, the XL9555, touch and the IMU. 0x77 is tried as
 * well when 0x76 is silent. */
#define ENV_I2C_BUS "i2c1"
#define ENV_ADDR 0x76
#define ENV_ADDR_ALT 0x77
#define ENV_PERIOD_MS 10000     /* one sample every 10 s */
#define ENV_FIRST_MS 500        /* first probe; the chip needs 2 ms after power-up */
#define ENV_RETRY_MAX_MS 300000 /* not found: back off to one probe per 5 min */
#define ENV_STALE_MS 35000      /* older samples are not shown */
#define ENV_STACK_SIZE 2048
/* Below ac_send (12) and main (15), above ui and tshell (20): the i2c1 bus
 * lock is FIFO and RT-Thread 3.1.3 does not boost the thread it hands the
 * lock to (ipc.c:833-850), so an env owner must not be preemptible by ui or
 * tshell while ac_send waits behind it. env works ~1-2 ms per 10 s. */
#define ENV_PRIORITY 19
#define ENV_EVT_KICK 0x01
#define ENV_CFG_PATH "/env_sensor.cfg"
#define ENV_CFG_VERSION 1
#define ENV_TEMP_OFF_MAX 100 /* +-10.0 C */
#define ENV_RH_OFF_MAX 200   /* +-20.0 %RH */
/* Magnus formula over water, Sonntag (1990) coefficients, -45..60 C. */
#define MAGNUS_A 17.62f
#define MAGNUS_B 243.12f

/* BME280 registers (Bosch data sheet BST-BME280-DS001 rev 1.23, table 18). */
#define REG_CALIB_TP 0x88 /* 0x88..0xA1: T1-T3, P1-P9, (0xA0), H1 */
#define REG_ID 0xD0
#define REG_RESET 0xE0
#define REG_CALIB_H 0xE1 /* 0xE1..0xE7: H2-H6 */
#define REG_CTRL_HUM 0xF2
#define REG_STATUS 0xF3
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG 0xF5
#define REG_DATA 0xF7 /* 0xF7..0xFE: pressure, temperature (20 bit), humidity */
#define BME280_ID 0x60
#define RESET_WORD 0xB6
#define STATUS_MEASURING 0x08
#define STATUS_IM_UPDATE 0x01
/* One forced-mode conversion with oversampling x1 and the IIR filter off,
 * Bosch's "weather monitoring" setting (section 3.5.1); it takes at most
 * 9.3 ms (section 9.1). ctrl_hum takes effect with the next ctrl_meas write. */
#define CTRL_HUM_X1 0x01
#define CTRL_MEAS_X1_FORCED 0x25 /* osrs_t x1, osrs_p x1, mode forced */
#define CONFIG_FILTER_OFF 0x00
#define MEAS_WAIT_MS 10
/* Data register reset values, i.e. nothing measured since a chip reset. */
#define RESET_ADC_TP 0x80000
#define RESET_ADC_H 0x8000

typedef struct
{
    uint16_t t1;
    int16_t t2, t3;
    uint16_t p1;
    int16_t p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1, h3;
    int16_t h2, h4, h5;
    int8_t h6;
} calib_t;

typedef struct
{
    uint8_t version;
    uint8_t reserved;
    int16_t temp_off_dc;
    int16_t rh_off_d;
} env_cfg_t;

/* Reader thread only. */
static struct rt_i2c_bus_device *s_bus;
static calib_t s_cal;
static bool s_ready; /* chip found, reset and calibration loaded */
static uint8_t s_addr = ENV_ADDR;
static uint32_t s_backoff_ms = ENV_PERIOD_MS;
static rt_tick_t s_probe_tick;
static ir_power_t s_last_pwr; /* ir_power state at the previous cycle */

/* Shared with the UI and the console, under s_lock. */
static struct rt_mutex s_lock;
static struct rt_event s_kick;
static bool s_inited;
static bool s_started;
static env_status_t s_status = ENV_STARTING;
static const char *s_detail = "";
static bool s_have_sample;
static rt_tick_t s_sample_tick;
static float s_raw_t;  /* C */
static float s_raw_rh; /* %RH */
static int16_t s_hpa_d;
static int16_t s_temp_off_dc;
static int16_t s_rh_off_d;
static uint8_t s_found_addr;
static uint32_t s_samples;
static uint32_t s_errors;
static volatile uint32_t s_seq;
static volatile uint32_t s_cycles;

/* ------------------------------------------------------------------ */
/* BME280 access                                                       */

static int bme_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    struct rt_i2c_msg msgs[2];

    memset(msgs, 0, sizeof(msgs));
    msgs[0].addr = s_addr;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = &reg;
    msgs[0].len = 1;
    msgs[1].addr = s_addr;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = buf;
    msgs[1].len = len;
    return rt_i2c_transfer(s_bus, msgs, 2) == 2 ? 0 : -1;
}

/* Register / value pairs in one write (data sheet section 6.2.1). */
static int bme_write(uint8_t *pairs, uint16_t len)
{
    struct rt_i2c_msg msg;

    memset(&msg, 0, sizeof(msg));
    msg.addr = s_addr;
    msg.flags = RT_I2C_WR;
    msg.buf = pairs;
    msg.len = len;
    return rt_i2c_transfer(s_bus, &msg, 1) == 1 ? 0 : -1;
}

static uint16_t u16le(const uint8_t *b)
{
    return (uint16_t)(b[0] | (b[1] << 8));
}

/* Data sheet table 16. */
static void parse_calib(const uint8_t tp[26], const uint8_t h[7], calib_t *c)
{
    c->t1 = u16le(tp + 0);
    c->t2 = (int16_t)u16le(tp + 2);
    c->t3 = (int16_t)u16le(tp + 4);
    c->p1 = u16le(tp + 6);
    c->p2 = (int16_t)u16le(tp + 8);
    c->p3 = (int16_t)u16le(tp + 10);
    c->p4 = (int16_t)u16le(tp + 12);
    c->p5 = (int16_t)u16le(tp + 14);
    c->p6 = (int16_t)u16le(tp + 16);
    c->p7 = (int16_t)u16le(tp + 18);
    c->p8 = (int16_t)u16le(tp + 20);
    c->p9 = (int16_t)u16le(tp + 22);
    c->h1 = tp[25];
    c->h2 = (int16_t)u16le(h + 0);
    c->h3 = h[2];
    /* Two 12-bit values share 0xE5; their sign is in 0xE4 and 0xE6. */
    c->h4 = (int16_t)((int8_t)h[3] * 16 + (h[4] & 0x0F));
    c->h5 = (int16_t)((int8_t)h[5] * 16 + (h[4] >> 4));
    c->h6 = (int8_t)h[6];
}

/* Bosch's fixed-point compensation (data sheet section 4.2.3, rev. 1.1),
 * with 64-bit intermediates so that odd input cannot overflow; for real
 * input the results are identical. Temperature in 0.01 C. */
static int32_t comp_temp(const calib_t *c, int32_t adc, int32_t *t_fine)
{
    int64_t d = (int64_t)(adc >> 4) - c->t1;
    int32_t var1 = (int32_t)((((int64_t)(adc >> 3) - 2 * (int64_t)c->t1) * c->t2) >> 11);
    int32_t var2 = (int32_t)((((d * d) >> 12) * c->t3) >> 14);

    *t_fine = var1 + var2;
    return (*t_fine * 5 + 128) >> 8;
}

/* Pressure in Pa as Q24.8, 0 if the calibration is unusable. */
static uint32_t comp_press(const calib_t *c, int32_t adc, int32_t t_fine)
{
    int64_t var1, var2, p;

    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * c->p6;
    var2 = var2 + var1 * c->p5 * 131072;
    var2 = var2 + (int64_t)c->p4 * 34359738368LL;
    var1 = ((var1 * var1 * c->p3) >> 8) + var1 * c->p2 * 4096;
    var1 = ((((int64_t)1 << 47) + var1) * c->p1) >> 33;
    if (var1 == 0)
    {
        return 0; /* avoid a division by zero */
    }
    p = 1048576 - adc;
    p = (p * 2147483648LL - var2) * 3125 / var1;
    var1 = ((int64_t)c->p9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)c->p8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (int64_t)c->p7 * 16;
    return (uint32_t)p;
}

/* Relative humidity as Q22.10 (1/1024 %RH), 0..100 %RH. */
static uint32_t comp_hum(const calib_t *c, int32_t adc, int32_t t_fine)
{
    int64_t v = (int64_t)t_fine - 76800;
    int64_t x = ((int64_t)adc * 16384 - (int64_t)c->h4 * 1048576 - c->h5 * v + 16384) >> 15;
    int64_t y = (((((v * c->h6) >> 10) * (((v * c->h3) >> 11) + 32768)) >> 10) + 2097152) * c->h2;

    v = x * ((y + 8192) >> 14);
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * c->h1) >> 4);
    v = v < 0 ? 0 : v;
    v = v > 419430400 ? 419430400 : v;
    return (uint32_t)(v >> 12);
}

/* ------------------------------------------------------------------ */
/* Shared state                                                        */

/* True when the status changed (the caller logs it). */
static bool set_status(env_status_t st, const char *detail)
{
    bool changed;

    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    changed = st != s_status || strcmp(detail, s_detail) != 0;
    s_status = st;
    s_detail = detail;
    if (changed)
    {
        s_seq++;
    }
    rt_mutex_release(&s_lock);
    return changed;
}

static void count_error(void)
{
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    s_errors++;
    rt_mutex_release(&s_lock);
}

static void store_sample(float t, float rh, int16_t hpa_d)
{
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    s_raw_t = t;
    s_raw_rh = rh;
    s_hpa_d = hpa_d;
    s_have_sample = true;
    s_sample_tick = rt_tick_get();
    s_found_addr = s_addr;
    s_samples++;
    s_seq++;
    rt_mutex_release(&s_lock);
}

static int16_t tenths(float v)
{
    return (int16_t)(v < 0.0f ? v * 10.0f - 0.5f : v * 10.0f + 0.5f);
}

static float magnus(float t)
{
    return MAGNUS_A * t / (MAGNUS_B + t);
}

/* ------------------------------------------------------------------ */
/* Reader thread                                                       */

static int chip_init(void)
{
    uint8_t cmd[2], tp[26], h[7], st;
    calib_t c;

    cmd[0] = REG_RESET;
    cmd[1] = RESET_WORD;
    if (bme_write(cmd, sizeof(cmd)) != 0)
    {
        return -1;
    }
    rt_thread_mdelay(3); /* power-on-reset sequence: 2 ms start-up */
    for (int i = 0;; i++)
    {
        if (bme_read(REG_STATUS, &st, 1) != 0)
        {
            return -1;
        }
        if (!(st & STATUS_IM_UPDATE))
        {
            break; /* calibration copied from NVM */
        }
        if (i == 10)
        {
            return -1;
        }
        rt_thread_mdelay(2);
    }
    if (bme_read(REG_CALIB_TP, tp, sizeof(tp)) != 0 || bme_read(REG_CALIB_H, h, sizeof(h)) != 0)
    {
        return -1;
    }
    parse_calib(tp, h, &c);
    if (c.t1 == 0 || c.t1 == 0xFFFF || c.p1 == 0)
    {
        return -1; /* not a calibration */
    }
    /* Still in sleep mode after the reset, so this write is not ignored. */
    cmd[0] = REG_CONFIG;
    cmd[1] = CONFIG_FILTER_OFF;
    if (bme_write(cmd, sizeof(cmd)) != 0)
    {
        return -1;
    }
    s_cal = c;
    return 0;
}

/* Find the chip and load its calibration; on failure the status says why.
 * both: also try the other address. */
static bool attach(bool both)
{
    uint8_t first = s_addr;
    uint8_t id = 0;
    int r;

    s_ready = false;
    r = bme_read(REG_ID, &id, 1);
    if (r != 0 && both)
    {
        s_addr = (first == ENV_ADDR) ? ENV_ADDR_ALT : ENV_ADDR;
        r = bme_read(REG_ID, &id, 1);
    }
    if (r != 0 || id != BME280_ID)
    {
        if (r != 0)
        {
            if (set_status(ENV_NOT_FOUND, "no answer at 0x76/0x77 (WIFI_3V3 off? battery switch?)"))
            {
                LOG_W("BME280 not found: no answer at 0x76/0x77 on %s (WIFI_3V3 off? battery switch?)",
                      ENV_I2C_BUS);
            }
        }
        else if (set_status(ENV_NOT_FOUND, "wrong chip id"))
        {
            LOG_W("BME280 not found: the chip at 0x%02x has id 0x%02x, not 0x60", s_addr, id);
        }
        s_addr = first;
        return false;
    }
    if (chip_init() != 0)
    {
        if (set_status(ENV_READ_ERROR, "init failed"))
        {
            LOG_W("BME280 at 0x%02x: reset or calibration read failed", s_addr);
        }
        return false;
    }
    s_ready = true;
    return true;
}

/* One forced-mode measurement. Returns NULL, or what went wrong. */
static const char *measure(void)
{
    uint8_t trig[4] = {REG_CTRL_HUM, CTRL_HUM_X1, REG_CTRL_MEAS, CTRL_MEAS_X1_FORCED};
    uint8_t st, d[8];
    int32_t adc_p, adc_t, adc_h, t_fine, t;
    uint32_t p, h;

    /* Both control registers on every sample: a chip that was reset in
     * between (brown-out) still measures humidity. */
    if (bme_write(trig, sizeof(trig)) != 0)
    {
        return "I2C error";
    }
    rt_thread_mdelay(MEAS_WAIT_MS);
    for (int i = 0;; i++)
    {
        if (bme_read(REG_STATUS, &st, 1) != 0)
        {
            return "I2C error";
        }
        if (!(st & STATUS_MEASURING))
        {
            break;
        }
        if (i == 10)
        {
            return "measurement timed out";
        }
        rt_thread_mdelay(2);
    }
    /* One burst read, so all three values come from the same conversion. */
    if (bme_read(REG_DATA, d, sizeof(d)) != 0)
    {
        return "I2C error";
    }
    adc_p = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4));
    adc_t = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4));
    adc_h = (int32_t)(((uint32_t)d[6] << 8) | d[7]);
    /* Each value alone can be genuine (0x80000 is about 26 C); all three
     * together mean the conversion never ran. */
    if (adc_p == RESET_ADC_TP && adc_t == RESET_ADC_TP && adc_h == RESET_ADC_H)
    {
        return "no conversion";
    }
    t = comp_temp(&s_cal, adc_t, &t_fine);
    p = comp_press(&s_cal, adc_p, t_fine);
    h = comp_hum(&s_cal, adc_h, t_fine);
    /* Operating range -40..85 C, 300..1100 hPa. */
    if (t < -4000 || t > 8500 || p < 300UL * 100 * 256 || p > 1100UL * 100 * 256)
    {
        return "implausible values";
    }
    store_sample((float)t / 100.0f, (float)h / 1024.0f, (int16_t)((p + 1280) / 2560));
    return RT_NULL;
}

static void log_ok(void)
{
    env_reading_t r;
    char t[12], rh[12], p[12];

    env_sensor_get(&r);
    env_fmt_tenths(t, sizeof(t), r.raw_temp_dc, false);
    env_fmt_tenths(rh, sizeof(rh), r.raw_rh_d, false);
    env_fmt_tenths(p, sizeof(p), r.hpa_d, false);
    LOG_I("BME280 at 0x%02x: %s C %s %%RH %s hPa (forced x1, filter off, every %d s)", r.addr, t, rh,
          p, ENV_PERIOD_MS / 1000);
}

/* WIFI_3V3, the sensor's supply, needs the keyboard board, WIFI_EN high
 * and a battery (ir_power.h). */
static bool rail_expected(ir_power_t p)
{
    return p != IR_POWER_NO_KEYBOARD && p != IR_POWER_EN_FAILED && p != IR_POWER_NO_BATTERY;
}

static void cycle(bool force)
{
    ir_power_t pwr = ir_power_state();
    bool rail_back = rail_expected(pwr) && !rail_expected(s_last_pwr);
    const char *err;

    s_last_pwr = pwr;
    if (!s_ready)
    {
        if (pwr == IR_POWER_NO_KEYBOARD)
        {
            /* The sensor is on the keyboard board: nothing to probe. */
            if (set_status(ENV_NOT_FOUND, "no keyboard board"))
            {
                LOG_W("BME280 not found: no keyboard board");
            }
            return;
        }
        if (rail_back)
        {
            s_backoff_ms = ENV_PERIOD_MS; /* battery or WIFI_EN back: probe now */
        }
        else if (!force && s_status != ENV_STARTING &&
                 rt_tick_get() - s_probe_tick < rt_tick_from_millisecond(s_backoff_ms))
        {
            return;
        }
        s_probe_tick = rt_tick_get();
        /* Both addresses on the first miss; then only the usual one, as
         * every miss costs two "bus err" lines from drv_i2c.c. */
        if (!attach(force || s_status != ENV_NOT_FOUND))
        {
            if (s_status == ENV_NOT_FOUND)
            {
                s_backoff_ms = s_backoff_ms >= ENV_RETRY_MAX_MS / 2 ? ENV_RETRY_MAX_MS : 2 * s_backoff_ms;
            }
            return;
        }
        s_backoff_ms = ENV_PERIOD_MS;
    }
    err = measure();
    if (err != RT_NULL)
    {
        /* One retry after a fresh reset absorbs a glitch; a chip that went
         * away (keyboard board re-seated) is probed again from scratch. */
        if (!attach(false))
        {
            count_error();
            return;
        }
        err = measure();
    }
    if (err != RT_NULL)
    {
        s_ready = false;
        count_error();
        if (set_status(ENV_READ_ERROR, err))
        {
            LOG_W("BME280 read error: %s", err);
        }
        return;
    }
    if (set_status(ENV_OK, ""))
    {
        log_ok();
    }
}

static void env_thread(void *param)
{
    rt_uint32_t ev;
    bool force = false;

    (void)param;
    rt_thread_mdelay(ENV_FIRST_MS);
    while (1)
    {
        cycle(force);
        s_cycles++;
        force = rt_event_recv(&s_kick, ENV_EVT_KICK, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                              rt_tick_from_millisecond(ENV_PERIOD_MS), &ev) == RT_EOK;
    }
}

/* ------------------------------------------------------------------ */
/* Calibration offsets on the flash filesystem                        */

static int cfg_save(int16_t temp_off_dc, int16_t rh_off_d)
{
#ifdef RT_USING_DFS
    env_cfg_t c;
    bool ok;
    int fd;

    memset(&c, 0, sizeof(c));
    c.version = ENV_CFG_VERSION;
    c.temp_off_dc = temp_off_dc;
    c.rh_off_d = rh_off_d;
    fd = open(ENV_CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        LOG_W("cannot write %s", ENV_CFG_PATH);
        return -1;
    }
    ok = write(fd, &c, sizeof(c)) == (int)sizeof(c);
    close(fd);
    return ok ? 0 : -1;
#else
    (void)temp_off_dc;
    (void)rh_off_d;
    return -1;
#endif
}

static void cfg_load(void)
{
#ifdef RT_USING_DFS
    env_cfg_t c;
    char t[12], rh[12];
    int fd = open(ENV_CFG_PATH, O_RDONLY, 0);

    if (fd < 0)
    {
        return;
    }
    if (read(fd, &c, sizeof(c)) == (int)sizeof(c) && c.version == ENV_CFG_VERSION &&
        c.temp_off_dc >= -ENV_TEMP_OFF_MAX && c.temp_off_dc <= ENV_TEMP_OFF_MAX &&
        c.rh_off_d >= -ENV_RH_OFF_MAX && c.rh_off_d <= ENV_RH_OFF_MAX)
    {
        s_temp_off_dc = c.temp_off_dc;
        s_rh_off_d = c.rh_off_d;
        env_fmt_tenths(t, sizeof(t), c.temp_off_dc, true);
        env_fmt_tenths(rh, sizeof(rh), c.rh_off_d, true);
        LOG_I("room sensor offsets %s C %s %%RH (from %s)", t, rh, ENV_CFG_PATH);
    }
    close(fd);
#endif
}

/* ------------------------------------------------------------------ */
/* API                                                                 */

void env_sensor_init(void)
{
    rt_thread_t th;

    rt_mutex_init(&s_lock, "env", RT_IPC_FLAG_PRIO);
    rt_event_init(&s_kick, "envkick", RT_IPC_FLAG_PRIO);
    cfg_load();
    s_inited = true;

    s_bus = rt_i2c_bus_device_find(ENV_I2C_BUS);
    if (s_bus == RT_NULL)
    {
        set_status(ENV_NOT_FOUND, "no I2C bus");
        LOG_E("no I2C bus \"%s\": room sensor disabled", ENV_I2C_BUS);
        return;
    }
    th = rt_thread_create("env", env_thread, RT_NULL, ENV_STACK_SIZE, ENV_PRIORITY, 10);
    if (th == RT_NULL)
    {
        set_status(ENV_NOT_FOUND, "no reader thread");
        LOG_E("cannot start the room-sensor thread");
        return;
    }
    s_started = true;
    rt_thread_startup(th);
}

void env_sensor_get(env_reading_t *r)
{
    rt_tick_t age = 0;
    bool have = false;
    float t = 0.0f, rh = 0.0f, tc, rhc;

    memset(r, 0, sizeof(*r));
    r->status = ENV_STARTING;
    r->detail = "";
    if (s_inited)
    {
        rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
        r->status = s_status;
        r->detail = s_detail;
        have = s_have_sample;
        age = rt_tick_get() - s_sample_tick;
        t = s_raw_t;
        rh = s_raw_rh;
        r->hpa_d = s_hpa_d;
        r->temp_off_dc = s_temp_off_dc;
        r->rh_off_d = s_rh_off_d;
        r->addr = s_found_addr;
        r->samples = s_samples;
        r->errors = s_errors;
        rt_mutex_release(&s_lock);
    }
    r->age_s = have ? (int32_t)(age / RT_TICK_PER_SECOND) : -1;
    r->valid = have && r->status == ENV_OK && age <= rt_tick_from_millisecond(ENV_STALE_MS);
    if (have && r->status == ENV_OK && !r->valid)
    {
        r->status = ENV_READ_ERROR; /* the reader thread is stuck */
        r->detail = "no recent sample";
    }
    if (!r->valid)
    {
        r->temp_dc = r->rh_d = r->hpa_d = r->dew_dc = ENV_NO_VALUE;
        r->raw_temp_dc = r->raw_rh_d = ENV_NO_VALUE;
        return;
    }
    tc = t + (float)r->temp_off_dc / 10.0f;
    rhc = rh;
    if (r->temp_off_dc != 0)
    {
        /* Same water-vapour pressure in the room as at the sensor: scale by
         * the ratio of the saturation pressures (Magnus). */
        rhc = rh * expf(magnus(t) - magnus(tc));
    }
    rhc += (float)r->rh_off_d / 10.0f;
    r->raw_temp_dc = tenths(t);
    r->raw_rh_d = tenths(rh);
    r->temp_dc = tenths(tc);
    if (rhc > 0.0f)
    {
        /* From the unclamped value, so the dew point stays the measured one
         * even when it is above the corrected temperature. */
        float g = logf(rhc / 100.0f) + magnus(tc);

        r->dew_dc = tenths(MAGNUS_B * g / (MAGNUS_A - g));
    }
    else
    {
        r->dew_dc = ENV_NO_VALUE;
    }
    if (rhc > 100.0f)
    {
        r->detail = "temperature offset puts the dew point above the room temperature";
    }
    rhc = rhc < 0.0f ? 0.0f : (rhc > 100.0f ? 100.0f : rhc);
    r->rh_d = tenths(rhc);
}

uint32_t env_sensor_seq(void)
{
    return s_seq;
}

void env_sensor_refresh(uint32_t wait_ms)
{
    uint32_t start = s_cycles;

    if (!s_started)
    {
        return;
    }
    rt_event_send(&s_kick, ENV_EVT_KICK);
    for (uint32_t waited = 0; waited < wait_ms && s_cycles == start; waited += 10)
    {
        rt_thread_mdelay(10);
    }
}

int env_sensor_set_offsets(int temp_off_dc, int rh_off_d)
{
    if (temp_off_dc < -ENV_TEMP_OFF_MAX || temp_off_dc > ENV_TEMP_OFF_MAX ||
        rh_off_d < -ENV_RH_OFF_MAX || rh_off_d > ENV_RH_OFF_MAX)
    {
        return -1;
    }
    if (!s_inited)
    {
        return -2;
    }
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    s_temp_off_dc = (int16_t)temp_off_dc;
    s_rh_off_d = (int16_t)rh_off_d;
    s_seq++;
    rt_mutex_release(&s_lock);
    return cfg_save((int16_t)temp_off_dc, (int16_t)rh_off_d) == 0 ? 0 : -2;
}

const char *env_status_str(env_status_t s)
{
    switch (s)
    {
    case ENV_OK:
        return "ok";
    case ENV_NOT_FOUND:
        return "not found";
    case ENV_READ_ERROR:
        return "read error";
    default:
        return "starting";
    }
}

void env_fmt_tenths(char *buf, size_t len, int32_t v, bool plus)
{
    uint32_t a = v < 0 ? (uint32_t)(-v) : (uint32_t)v;

    snprintf(buf, len, "%s%lu.%lu", v < 0 ? "-" : (plus ? "+" : ""), (unsigned long)(a / 10),
             (unsigned long)(a % 10));
}
