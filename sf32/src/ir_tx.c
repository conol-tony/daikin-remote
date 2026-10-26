#include "ir_tx.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>

#include "bf0_hal.h"
#include "drv_io.h"
#include "board.h"

#ifdef RT_USING_PM
#include <drivers/pm.h>
#endif

#define IR_PWM_NAME "pwm3"   /* GPTIM2 on SF32LB52x */
#define IR_PWM_CHANNEL 1     /* GPTIM2_CH1 = PA36 */
#define IR_TIMER hwp_gptim2

/* Spaces at least this long are the inter-section gaps; interrupts are
 * allowed again while waiting them out. */
#define IR_GAP_THRESHOLD_US 5000

static struct rt_device_pwm *s_pwm;
static uint32_t s_ccr_on;
static uint32_t s_cycles_per_us;
static bool s_ready;

static inline uint32_t cycles_now(void)
{
    return HAL_DBG_DWT_GetCycles();
}

static inline void carrier(bool on)
{
    IR_TIMER->CCR1 = on ? s_ccr_on : 0;
}

/* Busy-wait until the absolute cycle deadline has passed. */
static inline void wait_until(uint32_t deadline)
{
    while ((int32_t)(cycles_now() - deadline) < 0)
    {
    }
}

int ir_tx_init(uint32_t carrier_hz, uint8_t duty_percent)
{
    uint32_t period_ns = 1000000000UL / carrier_hz;
    uint32_t pulse_ns = period_ns * duty_percent / 100;

    s_ready = false;

    /* PA36 -> GPTIM2_CH1, no pull: the NPN base must float low when idle. */
    HAL_PIN_Set(PAD_PA36, GPTIM2_CH1, PIN_NOPULL, 1);

    s_pwm = (struct rt_device_pwm *)rt_device_find(IR_PWM_NAME);
    if (s_pwm == RT_NULL)
    {
        rt_kprintf("ir_tx: PWM device %s not found\n", IR_PWM_NAME);
        return -1;
    }
    if (rt_pwm_set(s_pwm, IR_PWM_CHANNEL, period_ns, pulse_ns) != RT_EOK)
    {
        rt_kprintf("ir_tx: rt_pwm_set failed\n");
        return -2;
    }
    /* Start the timer with the carrier configured, then park the output
     * low by zeroing the compare value; the timer keeps running. */
    if (rt_pwm_enable(s_pwm, IR_PWM_CHANNEL) != RT_EOK)
    {
        rt_kprintf("ir_tx: rt_pwm_enable failed\n");
        return -3;
    }
    s_ccr_on = IR_TIMER->CCR1;
    carrier(false);

    if (!HAL_DBG_DWT_IsInit())
    {
        HAL_DBG_DWT_Init();
    }
    s_cycles_per_us = HAL_RCC_GetHCLKFreq(CORE_ID_DEFAULT) / 1000000UL;

    rt_kprintf("ir_tx: %s ch%d on PA36, period %u ticks, pulse %u ticks, %u MHz core\n",
               IR_PWM_NAME, IR_PWM_CHANNEL, (unsigned)IR_TIMER->ARR + 1,
               (unsigned)s_ccr_on, (unsigned)s_cycles_per_us);
    s_ready = (s_ccr_on != 0);
    return s_ready ? 0 : -4;
}

bool ir_tx_ready(void)
{
    return s_ready;
}

int ir_tx_send(const uint32_t *timings, size_t count)
{
    rt_base_t level;
    uint32_t deadline, start, expected_us = 0, measured_us;
    uint32_t lxt_start, lxt_us;
    size_t i;

    if (!s_ready || count == 0)
    {
        return -1;
    }
    for (i = 0; i < count; i++)
    {
        expected_us += timings[i];
    }

#ifdef RT_USING_PM
    rt_pm_request(PM_SLEEP_MODE_IDLE); /* no deep sleep / clock scaling mid-frame */
#endif
    s_cycles_per_us = HAL_RCC_GetHCLKFreq(CORE_ID_DEFAULT) / 1000000UL;

    rt_enter_critical(); /* no thread switches for the whole frame */
    level = rt_hw_interrupt_disable();
    /* GTIMER runs from the 32.768 kHz crystal, independent of HCLK/DWT. */
    lxt_start = HAL_HPAON_READ_GTIMER();
    start = cycles_now();
    deadline = start;
    for (i = 0; i < count; i++)
    {
        bool mark = (i % 2 == 0);
        uint32_t us = timings[i];

        carrier(mark);
        deadline += us * s_cycles_per_us;
        if (!mark && us >= IR_GAP_THRESHOLD_US)
        {
            /* Long gap: let interrupts run, then mask again before the
             * next mark. Absolute deadlines keep the gap length exact. */
            rt_hw_interrupt_enable(level);
            wait_until(deadline);
            level = rt_hw_interrupt_disable();
        }
        else
        {
            wait_until(deadline);
        }
    }
    carrier(false);
    measured_us = (cycles_now() - start) / s_cycles_per_us;
    lxt_us = (uint32_t)(((uint64_t)(HAL_HPAON_READ_GTIMER() - lxt_start) * 1000000ULL) / 32768ULL);
    rt_hw_interrupt_enable(level);
    rt_exit_critical();

#ifdef RT_USING_PM
    rt_pm_release(PM_SLEEP_MODE_IDLE);
#endif
    /* "DWT" uses the same clock as the deadlines, so it always matches;
     * "LXT" is an independent crystal and catches a wrong core clock. */
    rt_kprintf("ir_tx: %u edges, %u us expected, %u us DWT, %u us LXT%s\n", (unsigned)count,
               (unsigned)expected_us, (unsigned)measured_us, (unsigned)lxt_us,
               (lxt_us + expected_us / 50 + 100 < expected_us ||
                lxt_us > expected_us + expected_us / 50 + 100)
                   ? "  TIMING ERROR"
                   : "");
    return 0;
}
