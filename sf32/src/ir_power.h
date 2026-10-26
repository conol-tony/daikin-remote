/*
 * Power for the IR LED on the T-SF32-Keyboard board.
 *
 * The VSMY14940 LED hangs off WIFI_3V3, made by an RT9080 regulator on the
 * keyboard board whose input is the BATTERY net (VBAT / BAT_OUTSIDE), not
 * USB. WIFI_3V3 is switched by XL9555 P0_2 (WIFI_EN). So light needs the
 * keyboard board, WIFI_EN high and a battery that is present and charged.
 * The SGM41562B charger must be configured by the firmware: at power-on it
 * is in "default mode" with charging disabled (CEB=1), and while USB is
 * connected its I2C watchdog power-cycles the whole board if nobody writes
 * to it.
 */
#ifndef IR_POWER_H
#define IR_POWER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    IR_POWER_UNKNOWN = 0,  /* not checked yet */
    IR_POWER_OK,           /* keyboard board found, WIFI_EN high, battery OK */
    IR_POWER_NO_KEYBOARD,  /* XL9555 absent: no on-board IR LED, PA36 only */
    IR_POWER_EN_FAILED,    /* WIFI_EN (XL9555 P0_2) does not read back high */
    IR_POWER_LOW_BATTERY,  /* battery too low for full LED current */
    IR_POWER_NO_BATTERY,   /* battery missing or cut off: LED rail is dead */
    IR_POWER_VBAT_UNKNOWN, /* battery voltage unreadable: LED supply unverified */
} ir_power_t;

/* Configure the charger, switch WIFI_3V3 on, hold the unused ESP32-C6 in
 * reset and measure the battery. Call once at boot, before ir_tx_init(). */
ir_power_t ir_power_init(void);

/* Re-read the battery voltage and WIFI_EN. Takes a few tens of ms (the ADC
 * driver also prints a "ch[7]voltage=" line). Call before transmitting,
 * never with interrupts disabled. */
ir_power_t ir_power_check(void);

/* Call about once a second from an idle loop. Repeats the measurement when
 * the last one (boot, send or poll) is a minute old, so the screen,
 * "ac status" and "ac json" follow the battery as it charges or drains.
 * On battery power (no USB) below 3550 mV it raises ir_power_battery_low()
 * and 30 s later switches the board off (charger shipping mode); USB or
 * the D key turns it on again. Returns true when something shown changed. */
bool ir_power_poll(void);

/* Keep the charger's I2C watchdog from power-cycling the board, and log
 * charger state changes. Call often (it rate-limits itself to one I2C
 * write every 10 s). */
void ir_power_service(void);

ir_power_t ir_power_state(void);
int ir_power_vbat_mv(void);  /* last battery reading in mV, -1 if unknown */
bool ir_power_battery_low(void); /* on battery, low: power-off pending */
const char *ir_power_str(ir_power_t s);
/* Charger summary from its status registers, e.g. "charging",
 * "paused: NTC cold", "no USB power", "not configured". */
const char *ir_power_charger_str(void);

#ifdef __cplusplus
}
#endif

#endif /* IR_POWER_H */
