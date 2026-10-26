/*
 * Daikin ARC466A41 remote emulator for the LilyGo T-Display-SF32.
 *
 * - IR: PA36 (GPTIM2_CH1 / "pwm3") drives the VSMY14940 LED on the
 *   keyboard expansion board (powered via the XL9555 WIFI_EN rail), or an
 *   external LED driver on header CON1 pin 14.
 * - UI: 480x480 AMOLED with LVGL, display buttons and 20-key keypad.
 * - Room sensor: BME280 on the keyboard board, also on WIFI_3V3
 *   (env_sensor.h).
 * - Console: "ac ..." commands on the 1000000 baud USB-C serial port.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"

#ifdef RT_USING_DFS
#include <dfs_fs.h>
#include "drv_flash.h"
#endif
#include "button.h"

#include "ac_ctrl.h"
#include "env_sensor.h"
#include "ir_power.h"
#include "ir_tx.h"
#include "keypad.h"
#include "ui.h"
#include "daikin_arc466.h"

#define LOG_TAG "main"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define PIN_BUTTON_C 33 /* active low */
#define PIN_BUTTON_A 34 /* active high, also the hibernate wake pin */
#define PIN_BUTTON_B 35 /* active low */

/* ------------------------------------------------------------------ */
/* Flash filesystem for state persistence                              */

#if defined(RT_USING_DFS) && defined(FS_REGION_START_ADDR)
#define FS_ROOT "root"
static int mnt_init(void)
{
    register_mtd_device(FS_REGION_START_ADDR, FS_REGION_SIZE, FS_ROOT);
    if (dfs_mount(FS_ROOT, "/", "elm", 0, 0) == 0)
    {
        LOG_I("flash filesystem mounted");
    }
    else
    {
        LOG_W("no filesystem on flash, formatting");
        if (dfs_mkfs("elm", FS_ROOT) == 0 && dfs_mount(FS_ROOT, "/", "elm", 0, 0) == 0)
        {
            LOG_I("flash filesystem created and mounted");
        }
        else
        {
            LOG_E("flash filesystem unavailable, settings will not persist");
        }
    }
    return RT_EOK;
}
INIT_ENV_EXPORT(mnt_init);
#endif

/* ------------------------------------------------------------------ */
/* Buttons                                                             */

static void button_handler(int32_t pin, button_action_t action)
{
    if (action == BUTTON_CLICKED)
    {
        if (pin == PIN_BUTTON_A)
            ui_post(keypad_available() ? UI_EVT_TOGGLE_HELP : UI_EVT_NEXT_FIELD);
        else if (pin == PIN_BUTTON_B) ui_post(UI_EVT_VALUE_UP);
        else if (pin == PIN_BUTTON_C) ui_post(UI_EVT_VALUE_DOWN);
    }
    else if (action == BUTTON_LONG_PRESSED)
    {
        if (pin == PIN_BUTTON_A) ui_post(UI_EVT_SEND);
        else if (pin == PIN_BUTTON_B) ui_post(UI_EVT_PREV_FIELD);
        else if (pin == PIN_BUTTON_C) ui_post(UI_EVT_TOGGLE_POWER);
    }
}

static void buttons_init(void)
{
    button_cfg_t cfg_a = {
        .pin = PIN_BUTTON_A,
        .mode = PIN_MODE_INPUT_PULLDOWN,
        .active_state = BUTTON_ACTIVE_HIGH,
        .button_handler = button_handler,
    };
    button_cfg_t cfg_b = {
        .pin = PIN_BUTTON_B,
        .mode = PIN_MODE_INPUT,
        .active_state = BUTTON_ACTIVE_LOW,
        .button_handler = button_handler,
    };
    button_cfg_t cfg_c = {
        .pin = PIN_BUTTON_C,
        .mode = PIN_MODE_INPUT,
        .active_state = BUTTON_ACTIVE_LOW,
        .button_handler = button_handler,
    };
    button_cfg_t *cfgs[] = {&cfg_a, &cfg_b, &cfg_c};

    for (int i = 0; i < 3; i++)
    {
        int32_t id = button_init(cfgs[i]);
        if (id < 0)
        {
            LOG_E("button_init pin %d failed", (int)cfgs[i]->pin);
        }
        else
        {
            button_enable(id);
        }
    }
}

int main(void)
{
    char buf[320];

    LOG_I("Daikin ARC466A41 emulator starting");
    rt_thread_mdelay(100);

    /* Charger, keyboard-board WIFI_3V3 rail and battery check (ir_power.c). */
    ir_power_init();
    /* BME280 reader; needs WIFI_3V3, which ir_power_init() switched on. */
    env_sensor_init();

    if (ir_tx_init(DAIKIN_CARRIER_HZ, DAIKIN_CARRIER_DUTY_PERCENT) != 0)
    {
        LOG_E("IR transmitter init failed");
    }
    ac_ctrl_init();

    if (ui_init() == 0)
    {
        ui_start();
        keypad_init();
    }
    else
    {
        LOG_E("display init failed, continuing headless");
    }
    buttons_init();

    ac_ctrl_describe(buf, sizeof(buf));
    rt_kprintf("state: ");
    ac_console_puts(buf);
    rt_kprintf("\nType 'ac help' for the remote-control commands.\n");

    while (1)
    {
        rt_thread_mdelay(1000);
        /* Re-measure the IR LED supply every minute (the battery charges or
         * drains) and switch off on a flat battery; see ir_power.h. */
        if (ir_power_poll())
        {
            ui_invalidate();
        }
    }
    return 0;
}
