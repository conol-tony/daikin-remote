/*
 * 38 kHz IR transmitter for the LilyGo T-Display-SF32.
 *
 * PA36 ("IR_PWM") is GPTIM2 channel 1, exposed by the SiFli SDK as PWM
 * device "pwm3" channel 1. The carrier runs continuously on the timer and
 * marks/spaces are produced by gating the compare register (CCR1 = duty
 * for a mark, 0 for a space). Edge timing uses the Cortex-M DWT cycle
 * counter with absolute deadlines, so per-edge overhead does not
 * accumulate, and interrupts are masked while a section is on air.
 *
 * On the keyboard expansion board PA36 drives an S8050 NPN low-side switch
 * for the VSMY14940 LED, so the idle level must be LOW.
 */
#ifndef IR_TX_H
#define IR_TX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 on success, negative on failure (PWM device not found). */
int ir_tx_init(uint32_t carrier_hz, uint8_t duty_percent);

bool ir_tx_ready(void);

/*
 * Send alternating mark/space durations in microseconds (even index =
 * mark). Blocks until the whole frame has been emitted. Returns 0 on
 * success.
 */
int ir_tx_send(const uint32_t *timings, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* IR_TX_H */
