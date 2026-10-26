#include "keypad.h"

#include <rtthread.h>
#include <rtdevice.h>

#include "ir_power.h"
#include "ui.h"

#define LOG_TAG "keypad"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static bool s_available;

bool keypad_available(void)
{
    return s_available;
}

#if defined(PKG_USING_PKG_KEY_BOARD) && defined(TCA8418_USING_RT_DEVICE)
#include "tca8418_rt_device.h"
#include "xl9555.h"

#define KEYPAD_STACK_SIZE 1536
#define KEYPAD_PRIORITY 19
#define KEYPAD_BATCH_SIZE 10

static rt_device_t s_keyboard;
static bool s_attempted;

/* Hardware layout, with the screen above the keypad:
 *   PREV   ESC   HOME   MAIL
 *   ENTER  1     2      3
 *   NEXT   4     5      6
 *   CLOSE  7     8      9
 *   STOP   *     0      #
 * Keep all state changes on the UI thread, like the display buttons. */
static void post_key(KeyCode key)
{
    ui_event_t event;

    switch (key)
    {
    case KEY_PREV:  event = UI_EVT_PREV_FIELD; break;
    case KEY_NEXT:  event = UI_EVT_NEXT_FIELD; break;
    case KEY_ENTER: event = UI_EVT_VALUE_UP; break;
    case KEY_CLOSE: event = UI_EVT_VALUE_DOWN; break;
    case KEY_ESC:   event = UI_EVT_TOGGLE_HELP; break;
    case KEY_HOME:  event = UI_EVT_HOME; break;
    case KEY_MAIL:  event = UI_EVT_SEND; break;
    case KEY_STOP:  event = UI_EVT_POWER_OFF; break;
    case KEY_0:     event = UI_EVT_TOGGLE_POWER; break;
    case KEY_1:     event = UI_EVT_CYCLE_MODE; break;
    case KEY_2:     event = UI_EVT_CYCLE_FAN; break;
    case KEY_3:     event = UI_EVT_TOGGLE_POWERFUL; break;
    case KEY_4:     event = UI_EVT_TOGGLE_SWING_V; break;
    case KEY_5:     event = UI_EVT_TOGGLE_SWING_H; break;
    case KEY_6:     event = UI_EVT_TOGGLE_ECONO; break;
    case KEY_7:     event = UI_EVT_TOGGLE_QUIET; break;
    case KEY_8:     event = UI_EVT_TOGGLE_SENSOR; break;
    case KEY_9:     event = UI_EVT_TOGGLE_COMFORT; break;
    case KEY_MUL:   event = UI_EVT_TEMP_DOWN; break;
    case KEY_WELL:  event = UI_EVT_TEMP_UP; break;
    default: return;
    }
    ui_post(event);
}

static void keypad_thread(void *parameter)
{
    key_board_event_msg_t events[KEYPAD_BATCH_SIZE];

    (void)parameter;
    while (1)
    {
        /* This SDK read takes an event count, not a byte count. The driver
         * removes release edges before placing presses in its queue. */
        rt_size_t count = rt_device_read(s_keyboard, 0, events,
                                         KEYPAD_BATCH_SIZE);
        for (rt_size_t i = 0; i < count; i++)
        {
            /* A hold must not toggle power or send a second IR frame. */
            if (!events[i].is_long_press)
                post_key(events[i].code);
        }
        /* A successful read normally blocks until a press. Avoid spinning
         * if the device ever returns early without an event. */
        if (count == 0)
            rt_thread_mdelay(20);
    }
}

static int keypad_probe(void)
{
    struct rt_i2c_bus_device *bus;
    rt_uint8_t config;
    rt_size_t count;
    ir_power_t power = ir_power_state();

    if (power == IR_POWER_NO_KEYBOARD || power == IR_POWER_EN_FAILED ||
        power == IR_POWER_NO_BATTERY)
        return -RT_ERROR;

    bus = rt_i2c_bus_device_find(KEY_BOARD_I2C_BUS_NAME);
    if (bus == RT_NULL ||
        rt_device_open((rt_device_t)bus, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
        return -RT_ERROR;

    /* ir_power_init() has already initialised the expander and enabled
     * WIFI_3V3. Release reset before probing; the SDK will reset the keypad
     * again when opened. Probe first because its failure path retains the
     * scanner semaphore/queue it allocates before checking the chip. */
    xl9555_digital_write(XL9555_KEY_RST_PIN, 1);
    xl9555_pin_mode(XL9555_KEY_RST_PIN, XL9555_PIN_OUTPUT);
    rt_thread_mdelay(2);
    count = rt_i2c_mem_read(bus, TCA8418_ADDRESS, CFG, 1, &config, 1);
    rt_device_close((rt_device_t)bus);
    return count == 1 ? RT_EOK : -RT_ERROR;
}

int keypad_init(void)
{
    rt_thread_t thread;
    rt_int32_t timeout = RT_WAITING_FOREVER;

    if (s_available)
        return RT_EOK;
    if (s_attempted)
        return -RT_ERROR;
    s_attempted = true;

    s_keyboard = rt_device_find(TCA8418_RT_DEVICE_NAME);
    if (s_keyboard == RT_NULL || keypad_probe() != RT_EOK)
    {
        LOG_W("keypad unavailable; display controls remain active");
        return -RT_ERROR;
    }
    if (rt_device_open(s_keyboard, RT_DEVICE_OFLAG_RDONLY) != RT_EOK)
    {
        LOG_W("keypad open failed; display controls remain active");
        return -RT_ERROR;
    }
    if (rt_device_control(s_keyboard, TCA8418_RT_CTRL_SET_READ_TIMEOUT,
                           &timeout) != RT_EOK)
    {
        rt_device_close(s_keyboard);
        LOG_E("keypad read configuration failed");
        return -RT_ERROR;
    }
    thread = rt_thread_create("keypad", keypad_thread, RT_NULL,
                              KEYPAD_STACK_SIZE, KEYPAD_PRIORITY, 10);
    if (thread == RT_NULL)
    {
        rt_device_close(s_keyboard);
        LOG_E("keypad thread allocation failed");
        return -RT_ENOMEM;
    }
    if (rt_thread_startup(thread) != RT_EOK)
    {
        rt_thread_delete(thread);
        rt_device_close(s_keyboard);
        LOG_E("keypad thread startup failed");
        return -RT_ERROR;
    }
    s_available = true;
    ui_invalidate();
    LOG_I("20-key keypad ready; ESC shows key guide");
    return RT_EOK;
}

#else

int keypad_init(void)
{
    LOG_W("keypad driver not enabled");
    return -RT_ENOSYS;
}

#endif
