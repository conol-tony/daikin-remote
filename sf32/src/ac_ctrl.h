/*
 * Remote-control state machine: owns the Daikin state, auto-send after
 * changes, the wall clock stamped into frames (from the on-chip RTC),
 * persistence on the flash filesystem, and the IR transmitter.
 */
#ifndef AC_CTRL_H
#define AC_CTRL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "daikin_arc466.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Delay between the last change and the automatic transmission. */
#define AC_AUTO_SEND_DELAY_MS 900

void ac_ctrl_init(void);

/* The state is shared between the UI thread, the shell and the sender
 * thread; hold the lock while reading or editing it. The lock is
 * recursive. */
void ac_ctrl_lock(void);
void ac_ctrl_unlock(void);
daikin_state_t *ac_ctrl_state(void);

/* Call after editing the state: schedules auto-send and persistence. */
void ac_ctrl_mark_changed(void);

/* Transmit the current state now. Returns 0 on success. */
int ac_ctrl_send(void);
/* Transmit an arbitrary 35-byte frame as-is. */
int ac_ctrl_send_raw(const uint8_t frame[DAIKIN_FRAME_BYTES]);
/* Encode the current state (with the live clock) into a frame. */
void ac_ctrl_build_frame(uint8_t out[DAIKIN_FRAME_BYTES]);

bool ac_ctrl_pending(void);         /* auto-send scheduled */
bool ac_ctrl_auto_send(void);
void ac_ctrl_set_auto_send(bool on);
uint32_t ac_ctrl_send_count(void);
/* Power bit (ON/OFF) of the last frame actually clocked out on PA36. */
bool ac_ctrl_last_sent_power(void);
bool ac_ctrl_ir_ready(void);

void ac_ctrl_reset_defaults(void);

/* Wall clock from the RTC: false if the RTC has never been set. */
bool ac_ctrl_clock_now(uint16_t *minutes, uint8_t *day);
/* Set the RTC time (and weekday, 1 = Sun ... 7 = Sat, 0 = keep). */
int ac_ctrl_set_clock(uint16_t minutes, uint8_t day);

/* Human-readable / JSON state summaries. */
void ac_ctrl_describe(char *buf, size_t len);
void ac_ctrl_json(char *buf, size_t len);
void ac_ctrl_frame_hex(const uint8_t frame[DAIKIN_FRAME_BYTES], char *buf, size_t len);

/* Print a long string to the console; rt_kprintf truncates at the console
 * buffer size (128 bytes), so this writes it in pieces. */
void ac_console_puts(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* AC_CTRL_H */
