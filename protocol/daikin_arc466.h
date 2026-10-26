/*
 * Daikin ARC466A41 remote protocol: the 280-bit / 35-byte, three-section
 * "DAIKIN" IR frame (IRremoteESP8266 decode_type DAIKIN, IRDaikinESP).
 *
 * Frame layout (all bytes sent LSB-first):
 *   Section 1 (8 bytes):  11 DA 27 00 C5 [id] [comfort] [sum]
 *   Section 2 (8 bytes):  11 DA 27 00 42 [clock lo] [day<<3 | clock hi] [sum]
 *   Section 3 (19 bytes): 11 DA 27 00 00 [mode/power/timers] [temp*2] 00
 *                         [fan<<4 | swingV] [swingH] [on timer lo]
 *                         [off lo<<4 | on hi] [off timer hi] [powerful|quiet]
 *                         00 [fixed] [sensor|econo|weekly] [fixed|mold] [sum]
 * Each section ends with an 8-bit sum of the section's preceding bytes.
 *
 * Wire format: 5 zero bits + mark + ~25 ms gap, then each section as
 * header mark/space, data bits (mark + short space = 0, mark + long space
 * = 1), trailing mark, ~35 ms gap. Carrier 38 kHz.
 *
 * Plain C99, no platform dependencies, so it can be unit-tested on a host
 * and used from C or C++ firmware.
 */
#ifndef DAIKIN_ARC466_H
#define DAIKIN_ARC466_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAIKIN_FRAME_BYTES 35
#define DAIKIN_TIMER_DISABLED 0x600 /* "25:36", the remote's sentinel */
#define DAIKIN_CARRIER_HZ 38000
#define DAIKIN_CARRIER_DUTY_PERCENT 33
/* Mark/space entries produced by daikin_to_timings(): leader + 3 sections. */
#define DAIKIN_TIMING_COUNT (12 + (2 + 64 * 2 + 2) * 2 + (2 + 152 * 2 + 2))

/* Section 3 byte 21 bits 4-6. */
typedef enum {
  DAIKIN_MODE_AUTO = 0,
  DAIKIN_MODE_DRY = 2,
  DAIKIN_MODE_COOL = 3,
  DAIKIN_MODE_HEAT = 4,
  DAIKIN_MODE_FAN = 6,
} daikin_mode_t;

/* Section 3 byte 24 high nibble. */
typedef enum {
  DAIKIN_FAN_1 = 3,
  DAIKIN_FAN_2 = 4,
  DAIKIN_FAN_3 = 5,
  DAIKIN_FAN_4 = 6,
  DAIKIN_FAN_5 = 7,
  DAIKIN_FAN_AUTO = 0xA,
  DAIKIN_FAN_QUIET = 0xB, /* indoor-unit quiet ("night" fan) */
} daikin_fan_t;

typedef struct {
  bool power;
  daikin_mode_t mode;
  uint8_t temp_half_deg; /* target in 0.5 C units (50 = 25.0 C) */
  daikin_fan_t fan;
  bool swing_v;          /* flap up/down swing */
  bool swing_h;          /* louvre left/right swing */
  bool powerful;         /* SUPER POWERFUL (20 minutes) */
  bool quiet;            /* OUTDOOR UNIT QUIET */
  bool econo;            /* ECONO */
  bool sensor;           /* INTELLIGENT EYE */
  bool comfort;          /* COMFORT AIRFLOW */
  bool mold;             /* mould-proof; not a button on the ARC466A41 */
  bool weekly_timer_off; /* byte 32 bit 7; the A41 sends 0 */
  bool on_timer;
  uint16_t on_timer_min; /* minutes since midnight */
  bool off_timer;
  uint16_t off_timer_min;
  uint16_t clock_min;    /* remote's wall clock, minutes since midnight */
  uint8_t clock_day;     /* 0 = unset, 1 = Sun ... 7 = Sat */
} daikin_state_t;

/*
 * Bytes that vary between remotes of the same protocol family. The A41
 * values are copied verbatim from real ARC466A41 captures (SmartIR PR
 * #1021, 407 frames); the library profile reproduces IRremoteESP8266's
 * defaults and is used by the host tests.
 */
typedef struct {
  uint8_t id_byte5;      /* section 1 byte 5 */
  uint8_t fixed_byte31;  /* section 3 byte 31 */
  uint8_t fixed_byte33;  /* section 3 byte 33 (mold bit is OR-ed in) */
  bool dry_temp_marker;  /* send 0xC0 as the temperature byte in DRY */
  bool fan_temp25;       /* send 25 C as the temperature byte in FAN */
} daikin_profile_t;

extern const daikin_profile_t DAIKIN_PROFILE_ARC466A41;
extern const daikin_profile_t DAIKIN_PROFILE_LIBRARY;

/* Pulse timings in microseconds. */
typedef struct {
  uint32_t leader_mark;  /* 5 x (leader_mark, leader_space) then leader_mark */
  uint32_t leader_space;
  uint32_t leader_gap;   /* space after the leader */
  uint32_t hdr_mark;
  uint32_t hdr_space;
  uint32_t bit_mark;
  uint32_t one_space;
  uint32_t zero_space;
  uint32_t section_gap;  /* space after each section's trailing mark */
} daikin_timing_t;

/* Measured from real ARC466A41 captures (what the remote itself emits). */
extern const daikin_timing_t DAIKIN_TIMING_MEASURED;
/* IRremoteESP8266's constants (used to cross-check its test vectors). */
extern const daikin_timing_t DAIKIN_TIMING_LIBRARY;

/* Defaults: off, COOL, 25.0 C, fan AUTO, everything else off/unset. */
void daikin_state_init(daikin_state_t *s);

/*
 * Clamp fields to what the protocol can carry and apply the remote's
 * mutual exclusions (Powerful cancels Econo and Quiet; Quiet and Econo may
 * be combined).
 */
void daikin_sanitize(daikin_state_t *s);

/* Build the 35-byte frame (checksums included). */
void daikin_encode(const daikin_state_t *s, const daikin_profile_t *profile,
                   uint8_t out[DAIKIN_FRAME_BYTES]);

/* Validate the three section checksums and the fixed header bytes. */
bool daikin_valid_frame(const uint8_t frame[DAIKIN_FRAME_BYTES]);

/*
 * Parse a frame back into a state. Profile bytes are ignored. Returns
 * false if the frame is not a valid DAIKIN frame.
 */
bool daikin_decode(const uint8_t frame[DAIKIN_FRAME_BYTES], daikin_state_t *out);

/*
 * Expand a frame into alternating mark/space durations (even index =
 * mark). Returns the number of entries written (DAIKIN_TIMING_COUNT), or 0
 * if max_out is too small.
 */
size_t daikin_to_timings(const uint8_t frame[DAIKIN_FRAME_BYTES],
                         const daikin_timing_t *t, uint32_t *out,
                         size_t max_out);

uint8_t daikin_section_sum(const uint8_t *bytes, size_t count);

const char *daikin_mode_name(daikin_mode_t m);
const char *daikin_fan_name(daikin_fan_t f);
const char *daikin_day_name(uint8_t day);
bool daikin_parse_mode(const char *text, daikin_mode_t *out);
bool daikin_parse_fan(const char *text, daikin_fan_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DAIKIN_ARC466_H */
