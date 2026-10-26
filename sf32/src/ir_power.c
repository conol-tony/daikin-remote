#include "ir_power.h"

#include <rtthread.h>
#include <rtdevice.h>

#include "bf0_hal.h"
#include "drv_io.h"

#ifdef PKG_USING_XL9555
#include "xl9555.h"
#endif
#ifdef SGM41562B_USING_RT_DEVICE
#include "sgm41562b.h"
#include "sgm41562b_rt_device.h"
#endif

#define LOG_TAG "irpwr"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define PIN_LDO_3V3_EN 41    /* display-board VDD3V3, which also feeds the XL9555 */
#define PIN_WAKE 34          /* button A (active high) wakes the SoC from shutdown */
#define VBAT_ADC_NAME "bat1" /* GPADC1: registered only with CONFIG_BSP_USING_ADC1 */
#define VBAT_ADC_CHANNEL 7   /* battery; drv_adc.c returns 0.1 mV, divider applied */
#define VBAT_OK_MV 3100      /* WIFI_3V3 >= ~3.05 V: LED still gets >= ~50 mA */
/* Below this the cell is missing or its protection cut off. With USB in and
 * charging on, a missing cell can read close to the 4.2 V charge voltage. */
#define VBAT_PRESENT_MV 2500
#define POLL_MS 60000 /* idle re-measurement: the cell charges or drains */
/* Low-battery power-off, on battery power only (VSYS ~= VBAT there). The
 * SoC needs > 3.5 V to start and LilyGo asks for a low-battery shutdown so
 * the cell is not deep-discharged; same policy as their factory firmware. */
#define VBAT_LOW_MV 3550
#define VBAT_LOW_HYST_MV 100
#define LOW_POLL_MS 5000
#define LOW_GRACE_MS 30000
#define CHARGER_FEED_MS 10000

static struct rt_mutex s_lock; /* one measurement at a time */
static ir_power_t s_state = IR_POWER_UNKNOWN;
static int s_vbat_mv = -1;
static bool s_expander;
static bool s_batt_low;
static rt_tick_t s_last_check;
static rt_tick_t s_low_since;
static const char *s_charger_str = "not configured";
#ifdef RT_USING_ADC
static rt_device_t s_adc;
#endif
#ifdef SGM41562B_USING_RT_DEVICE
static rt_device_t s_charger;
static rt_tick_t s_last_feed;
static int s_charger_seen = -1; /* last logged register summary */
#endif

static int read_vbat_mv(void)
{
#ifdef RT_USING_ADC
    rt_uint32_t v = 0;
    int mv = -1;

    if (s_adc == RT_NULL)
    {
        rt_device_t d = rt_device_find(VBAT_ADC_NAME);

        if (d == RT_NULL || rt_device_open(d, RT_DEVICE_OFLAG_RDONLY) != RT_EOK)
        {
            return -1;
        }
        s_adc = d;
    }
    if (rt_adc_enable((rt_adc_device_t)s_adc, VBAT_ADC_CHANNEL) != RT_EOK)
    {
        return -1;
    }
    /* rt_device_read() returns 0 when the conversion fails or the input is
     * over range, where rt_adc_read() would return a stale value or 50000
     * (5 V). drv_adc.c already applies the VBAT divider factor. */
    if (rt_device_read(s_adc, VBAT_ADC_CHANNEL, &v, sizeof(v)) == sizeof(v))
    {
        mv = (int)(v / 10);
    }
    rt_adc_disable((rt_adc_device_t)s_adc, VBAT_ADC_CHANNEL);
    return mv;
#else
    return -1;
#endif
}

#ifdef SGM41562B_USING_RT_DEVICE
static const char *charger_summary(rt_uint8_t r01, rt_uint8_t r08, rt_uint8_t r09)
{
    if (r01 & SGM41562B_CEB)
    {
        return "charging disabled";
    }
    if (!(r08 & SGM41562B_PG_STAT))
    {
        return "no USB power";
    }
    /* The NTC bits always show the live state; the others latch until read. */
    if (r09 & SGM41562B_NTC_FAULT_COLD)
    {
        return "paused: NTC cold";
    }
    if (r09 & SGM41562B_NTC_FAULT_HOT)
    {
        return "paused: NTC hot";
    }
    if (r09 & SGM41562B_STMR_FAULT)
    {
        return "stopped: safety timer";
    }
    if (r09 & SGM41562B_BAT_FAULT)
    {
        return "battery overvoltage";
    }
    if (r09 & SGM41562B_THEM_SD)
    {
        return "thermal shutdown";
    }
    if (r09 & SGM41562B_VIN_FAULT)
    {
        return "input fault";
    }
    switch ((r08 & SGM41562B_CHG_STAT_MASK) >> SGM41562B_CHG_STAT_SHIFT)
    {
    case CHG_STAT_PRE_CHARGE:
        return "pre-charge";
    case CHG_STAT_FAST_CHARGE:
        return "charging";
    case CHG_STAT_CHARGE_DONE:
        return "charge done";
    default:
        return "not charging";
    }
}

/* Log what the charger really does, at boot and whenever it changes:
 * default_config() ignores every I2C result. Opening the device also puts
 * the NTC input in battery-NTC mode, so a cell without a 10 kOhm NTC on
 * battery connector P2 reads "cold" and is never charged. */
static void charger_report(bool boot)
{
    sgm41562b_handle_t h = sgm41562b_get_handle();
    rt_uint8_t r01, r05, r08, r09, wd;
    int seen;

    if (h == RT_NULL || sgm41562b_read_reg(h, SGM41562B_REG_POWER_ON_CFG, &r01) != RT_EOK ||
        sgm41562b_read_reg(h, SGM41562B_REG_TERM_TIMER_CTRL, &r05) != RT_EOK ||
        sgm41562b_read_reg(h, SGM41562B_REG_SYSTEM_STATUS, &r08) != RT_EOK ||
        sgm41562b_read_reg(h, SGM41562B_REG_FAULT, &r09) != RT_EOK)
    {
        if (s_charger_seen != -2)
        {
            LOG_E("SGM41562B registers unreadable: charger state unknown");
        }
        s_charger_seen = -2;
        s_charger_str = "unknown";
        return;
    }
    if (boot && (r01 & SGM41562B_CEB))
    {
        /* One of default_config()'s writes was lost: try once more. */
        LOG_W("SGM41562B: charging still disabled after setup, retrying");
        if (sgm41562b_enable_charging(h, RT_TRUE) != RT_EOK ||
            sgm41562b_read_reg(h, SGM41562B_REG_POWER_ON_CFG, &r01) != RT_EOK)
        {
            r01 |= SGM41562B_CEB;
        }
    }
    s_charger_str = charger_summary(r01, r08, r09);
    /* WATCHDOG[1:0] is left out: the chip forces it to 40 s on every input
     * or interrupt event until the next write, i.e. the next feed. */
    seen = ((r01 & SGM41562B_CEB) << 16) |
           ((r08 & (SGM41562B_WTD_FAULT | SGM41562B_CHG_STAT_MASK | SGM41562B_PG_STAT)) << 8) |
           (r09 & 0x3F);
    if (!boot && seen == s_charger_seen)
    {
        return;
    }
    s_charger_seen = seen;
    wd = (rt_uint8_t)((r05 & SGM41562B_WATCHDOG_MASK) >> SGM41562B_WATCHDOG_SHIFT);
    /* Kept short: ulog truncates a line at ULOG_LINE_BUF_SIZE (128). */
    if (boot)
    {
        LOG_I("SGM41562B: charge %s, watchdog %s, USB %s: %s, faults 0x%02x",
              (r01 & SGM41562B_CEB) ? "DISABLED" : "enabled", wd ? "on (fed)" : "off",
              (r08 & SGM41562B_PG_STAT) ? "good" : "absent", s_charger_str, (unsigned)(r09 & 0x3F));
    }
    else
    {
        LOG_I("SGM41562B: USB %s: %s, faults 0x%02x", (r08 & SGM41562B_PG_STAT) ? "good" : "absent",
              s_charger_str, (unsigned)(r09 & 0x3F));
    }
    if (r08 & SGM41562B_WTD_FAULT)
    {
        LOG_W("SGM41562B: its watchdog expired (WTD_FAULT), which power-cycles the board");
    }
    if ((r08 & SGM41562B_PG_STAT) && (r09 & (SGM41562B_NTC_FAULT_COLD | SGM41562B_NTC_FAULT_HOT)))
    {
        LOG_W("SGM41562B: NTC reads %s, charging paused (cell needs a 10k NTC on P2 pin 2)",
              (r09 & SGM41562B_NTC_FAULT_COLD) ? "COLD" : "HOT");
    }
}

/* True only when the charger positively reports no input power. */
static bool on_battery_power(void)
{
    rt_uint8_t r08;

    return s_charger != RT_NULL &&
           rt_device_control(s_charger, SGM41562B_RT_CTRL_GET_STATUS, &r08) == RT_EOK &&
           !(r08 & SGM41562B_PG_STAT);
}

static void power_off(int mv)
{
    int8_t wp;

    LOG_E("battery %d mV and no USB power: switching off (USB or the D key turns it on)", mv);
    /* Fallback if the battery stays connected: PMU shutdown, woken by
     * button A, as in LilyGo's platform_power_shutdown(). */
    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);
    wp = HAL_HPAON_QueryWakeupPin(hwp_gpio1, PIN_WAKE);
    if (wp >= 0)
    {
        HAL_PMU_SelectWakeupPin(0, (uint8_t)wp);
        HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_HIGH);
    }
    /* FET_DIS: 1 s later the charger disconnects the battery from SYS. That
     * removes power from the whole board, the battery-fed RT9080 rails too:
     * their EN pull-downs win once the XL9555 loses VDD3V3. */
    rt_device_control(s_charger, SGM41562B_RT_CTRL_ENTER_SHIPPING, RT_NULL);
    rt_thread_mdelay(100); /* let the console drain */
    rt_pin_write(PIN_LDO_3V3_EN, PIN_LOW);
    HAL_PMU_EnterShutdown();
}
#endif

static void charger_init(void)
{
#ifdef SGM41562B_USING_RT_DEVICE
    rt_device_t chg = rt_device_find(SGM41562B_RT_DEVICE_NAME);

    /* Opening the device runs LilyGo's default_config(): charging enabled,
     * I2C watchdog disabled, 500 mA input limit, 4.2 V / 350 mA charge,
     * battery-NTC mode. */
    if (chg == RT_NULL || rt_device_open(chg, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("SGM41562B not configured: the battery (IR LED supply) will not charge");
        return;
    }
    s_charger = chg;
    /* Any write restores the programmed watchdog setting (off) that the chip
     * overrides on input or interrupt events; WD_RST is such a write. */
    rt_device_control(chg, SGM41562B_RT_CTRL_WATCHDOG_RESET, RT_NULL);
    s_last_feed = rt_tick_get();
    charger_report(true);
#else
    LOG_W("built without CONFIG_SGM41562B_USING_RT_DEVICE: the battery will not charge");
#endif
}

static ir_power_t classify(int vbat_mv)
{
    if (!s_expander)
    {
        return IR_POWER_NO_KEYBOARD;
    }
#ifdef PKG_USING_XL9555
    /* xl9555_digital_read() also returns 0 when the I2C read fails (and
     * drv_i2c.c resets the bus after a failure): ask twice. */
    if (xl9555_digital_read(XL9555_WIFI_EN_PIN) != 1 && xl9555_digital_read(XL9555_WIFI_EN_PIN) != 1)
    {
        return IR_POWER_EN_FAILED;
    }
#endif
    if (vbat_mv < 0)
    {
        /* No "bat1" ADC device or a failed conversion: never claim "ok". */
        return IR_POWER_VBAT_UNKNOWN;
    }
    if (vbat_mv < VBAT_PRESENT_MV)
    {
        return IR_POWER_NO_BATTERY;
    }
    if (vbat_mv < VBAT_OK_MV)
    {
        return IR_POWER_LOW_BATTERY;
    }
    return IR_POWER_OK;
}

/* Caller holds s_lock. */
static void measure(void)
{
    s_vbat_mv = read_vbat_mv();
    s_state = classify(s_vbat_mv);
    s_last_check = rt_tick_get();
}

static void log_state(void)
{
    if (s_state == IR_POWER_OK)
    {
        LOG_I("IR LED power ok (battery %d mV)", s_vbat_mv);
    }
    else
    {
        LOG_W("IR LED power: %s (battery %d mV)", ir_power_str(s_state), s_vbat_mv);
    }
}

/* Caller holds s_lock. */
static void battery_guard(void)
{
#ifdef SGM41562B_USING_RT_DEVICE
    int limit = VBAT_LOW_MV + (s_batt_low ? VBAT_LOW_HYST_MV : 0);

    /* An unknown voltage or charger state never switches the board off. */
    if (s_vbat_mv < VBAT_PRESENT_MV || s_vbat_mv >= limit || !on_battery_power())
    {
        if (s_batt_low)
        {
            s_batt_low = false;
            LOG_I("low-battery power-off cancelled (battery %d mV)", s_vbat_mv);
        }
        return;
    }
    if (!s_batt_low)
    {
        s_batt_low = true;
        s_low_since = rt_tick_get();
        LOG_W("battery low (%d mV) and no USB power: switching off in %d s", s_vbat_mv,
              LOW_GRACE_MS / 1000);
    }
    else if (rt_tick_get() - s_low_since >= rt_tick_from_millisecond(LOW_GRACE_MS))
    {
        power_off(s_vbat_mv);
    }
#endif
}

ir_power_t ir_power_init(void)
{
    rt_mutex_init(&s_lock, "irpwr", RT_IPC_FLAG_PRIO);

    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1);
    rt_pin_mode(PIN_LDO_3V3_EN, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LDO_3V3_EN, PIN_HIGH);
    rt_thread_mdelay(50);

    charger_init();

#ifdef PKG_USING_XL9555
    if (xl9555_init() == RT_EOK)
    {
        s_expander = true;
        /* The ESP32-C6 sits on the same battery-fed WIFI_3V3 rail and is not
         * used here: hold it in reset (P0_5 -> C6 EN) so it stops draining
         * the battery. */
        xl9555_digital_write(XL9555_WIFI_RST_PIN, 0);
        xl9555_pin_mode(XL9555_WIFI_RST_PIN, XL9555_PIN_OUTPUT);
        /* P0_2 (WIFI_EN) -> RT9080 EN -> WIFI_3V3 -> 27R -> VSMY14940.
         * Preload the latch, then make the pin an output (no glitch). */
        xl9555_digital_write(XL9555_WIFI_EN_PIN, 1);
        xl9555_pin_mode(XL9555_WIFI_EN_PIN, XL9555_PIN_OUTPUT);
        rt_thread_mdelay(20);
    }
    else
    {
        LOG_W("XL9555 not found: no keyboard board, so no on-board IR LED "
              "(an external LED on CON1 pin 14 still works)");
    }
#endif
    if (rt_device_find(VBAT_ADC_NAME) == RT_NULL)
    {
        LOG_E("no \"%s\" ADC device (CONFIG_BSP_USING_ADC1 not set?): "
              "the IR LED supply cannot be checked",
              VBAT_ADC_NAME);
    }
    return ir_power_check();
}

ir_power_t ir_power_check(void)
{
    ir_power_t s;

    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    measure();
    log_state();
    s = s_state;
    rt_mutex_release(&s_lock);
    return s;
}

bool ir_power_poll(void)
{
    ir_power_t prev_state;
    int prev_mv;
    bool prev_low, changed;

    if (rt_tick_get() - s_last_check < rt_tick_from_millisecond(s_batt_low ? LOW_POLL_MS : POLL_MS))
    {
        return false;
    }
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    prev_state = s_state;
    prev_mv = s_vbat_mv;
    prev_low = s_batt_low;
    measure();
    if (s_state != prev_state)
    {
        log_state();
    }
    battery_guard();
    changed = s_state != prev_state || s_vbat_mv != prev_mv || s_batt_low != prev_low;
    rt_mutex_release(&s_lock);
    return changed;
}

void ir_power_service(void)
{
#ifdef SGM41562B_USING_RT_DEVICE
    /* A USB plug-in or any charger interrupt pulse forces the SGM41562B
     * watchdog back to 40 s until the host writes to the chip; without that
     * write it power-cycles the board. WD_RST is such a write. */
    if (s_charger != RT_NULL &&
        rt_tick_get() - s_last_feed >= rt_tick_from_millisecond(CHARGER_FEED_MS))
    {
        s_last_feed = rt_tick_get();
        rt_device_control(s_charger, SGM41562B_RT_CTRL_WATCHDOG_RESET, RT_NULL);
        charger_report(false); /* logs only when something changed */
    }
#endif
}

ir_power_t ir_power_state(void)
{
    return s_state;
}

int ir_power_vbat_mv(void)
{
    return s_vbat_mv;
}

bool ir_power_battery_low(void)
{
    return s_batt_low;
}

const char *ir_power_str(ir_power_t s)
{
    switch (s)
    {
    case IR_POWER_OK:
        return "ok";
    case IR_POWER_NO_KEYBOARD:
        return "no keyboard board";
    case IR_POWER_EN_FAILED:
        return "WIFI_EN not high";
    case IR_POWER_LOW_BATTERY:
        return "battery low";
    case IR_POWER_NO_BATTERY:
        return "no battery";
    case IR_POWER_VBAT_UNKNOWN:
        return "battery not measured";
    default:
        return "unknown";
    }
}

const char *ir_power_charger_str(void)
{
    return s_charger_str;
}
