/*
 * Room temperature, humidity and pressure from the BME280 on the
 * T-SF32-Keyboard board: I2C1 (PA37/PA38) address 0x76, powered from
 * WIFI_3V3 like the IR LED (see ir_power.h). It only answers with the
 * keyboard board fitted, WIFI_EN high and the battery switched on.
 *
 * A low-priority thread takes one forced-mode measurement every 10 s
 * (oversampling x1, IIR filter off: Bosch's "weather monitoring" setting),
 * so the chip sleeps between samples and does not warm itself measurably.
 * The board around it is warmer than the room (display, SoC, charger,
 * battery): set a temperature offset to calibrate; the humidity is then
 * recomputed for the same water-vapour content at the corrected
 * temperature (Magnus formula), not just shifted.
 */
#ifndef ENV_SENSOR_H
#define ENV_SENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ENV_STARTING = 0, /* not probed yet */
    ENV_OK,           /* sampling */
    ENV_NOT_FOUND,    /* no BME280 answers (keyboard board, WIFI_3V3, battery switch) */
    ENV_READ_ERROR,   /* the chip answers but measuring failed, or no recent sample */
} env_status_t;

#define ENV_NO_VALUE INT16_MIN

/* A consistent snapshot. Values are in tenths (0.1 C, 0.1 %RH, 0.1 hPa)
 * and are ENV_NO_VALUE unless valid. */
typedef struct
{
    env_status_t status;
    bool valid;          /* status ok and the sample is recent: show it */
    int32_t age_s;       /* seconds since the last good sample, -1 = none yet */
    int16_t temp_dc;     /* corrected: measured + temperature offset */
    int16_t rh_d;        /* corrected: same dew point at temp_dc, + RH offset */
    int16_t hpa_d;
    int16_t dew_dc;      /* dew point (also ENV_NO_VALUE at 0 %RH) */
    int16_t raw_temp_dc; /* as measured */
    int16_t raw_rh_d;
    int16_t temp_off_dc; /* calibration offsets, always set */
    int16_t rh_off_d;
    uint8_t addr;        /* I2C address of the chip, 0 = never found */
    uint32_t samples;    /* good samples since boot */
    uint32_t errors;     /* failed measurements since boot */
    const char *detail;  /* reason for not found / read error, a note when OK, else "" */
} env_reading_t;

/* Load the saved offsets and start the reader thread. Call once at boot,
 * after ir_power_init() has switched WIFI_3V3 on. */
void env_sensor_init(void);

/* Snapshot for the UI and the console; any thread, never touches I2C. */
void env_sensor_get(env_reading_t *out);

/* Changes whenever a new sample, status or offset is stored. */
uint32_t env_sensor_seq(void);

/* Measure (or re-probe) now and wait up to wait_ms for the result. Sleeps:
 * console only. */
void env_sensor_refresh(uint32_t wait_ms);

/* Calibration offsets in 0.1 C (-100..100) and 0.1 %RH (-200..200), saved
 * on the flash filesystem. Returns 0 when saved, -1 when out of range
 * (nothing changed), -2 when applied but not saved. */
int env_sensor_set_offsets(int temp_off_dc, int rh_off_d);

const char *env_status_str(env_status_t s);

/* Tenths as text: "24.6", "-0.5"; with plus also "+1.5" and "+0.0". */
void env_fmt_tenths(char *buf, size_t len, int32_t v, bool plus);

#ifdef __cplusplus
}
#endif

#endif /* ENV_SENSOR_H */
