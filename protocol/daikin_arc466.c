#include "daikin_arc466.h"

#include <string.h>

#define MIN_TEMP_HALF 20 /* 10.0 C, the protocol floor */
#define MAX_TEMP_HALF 64 /* 32.0 C */

const daikin_profile_t DAIKIN_PROFILE_ARC466A41 = {0x10, 0xC5, 0x08, true, true};
const daikin_profile_t DAIKIN_PROFILE_LIBRARY = {0x00, 0xC0, 0x00, false, false};

/* Medians of 348 clean ARC466A41 captures (SmartIR PR #1021, Broadlink
 * tick 2^-15 s), cross-checked against Flipper / TSOP captures of sibling
 * remotes: bit cells 865 / 1735 us, header period 5.22 ms. */
const daikin_timing_t DAIKIN_TIMING_MEASURED = {450, 410, 25150, 3480, 1740,
                                                450, 1285, 415, 35550};
const daikin_timing_t DAIKIN_TIMING_LIBRARY = {428, 428, 29428, 3650, 1623,
                                               428, 1280, 428, 29428};

static bool equals_ignore_case(const char *a, const char *b) {
  for (;; a++, b++) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
    if (ca != cb) return false;
    if (ca == '\0') return true;
  }
}

static bool valid_mode(uint8_t m) {
  return m == 0 || m == 2 || m == 3 || m == 4 || m == 6;
}

static bool valid_fan(uint8_t f) { return (f >= 3 && f <= 7) || f == 0xA || f == 0xB; }

uint8_t daikin_section_sum(const uint8_t *bytes, size_t count) {
  uint8_t sum = 0;
  for (size_t i = 0; i < count; i++) sum = (uint8_t)(sum + bytes[i]);
  return sum;
}

void daikin_state_init(daikin_state_t *s) {
  memset(s, 0, sizeof(*s));
  s->mode = DAIKIN_MODE_COOL;
  s->temp_half_deg = 50;
  s->fan = DAIKIN_FAN_AUTO;
}

void daikin_sanitize(daikin_state_t *s) {
  if (s->temp_half_deg < MIN_TEMP_HALF) s->temp_half_deg = MIN_TEMP_HALF;
  if (s->temp_half_deg > MAX_TEMP_HALF) s->temp_half_deg = MAX_TEMP_HALF;
  if (!valid_mode((uint8_t)s->mode)) s->mode = DAIKIN_MODE_COOL;
  if (!valid_fan((uint8_t)s->fan)) s->fan = DAIKIN_FAN_AUTO;
  if (s->powerful) {
    s->econo = false;
    s->quiet = false;
  }
  if (s->on_timer_min >= 24 * 60) s->on_timer_min = 0;
  if (s->off_timer_min >= 24 * 60) s->off_timer_min = 0;
  if (s->clock_min >= 24 * 60) s->clock_min = 0;
  if (s->clock_day > 7) s->clock_day = 0;
}

void daikin_encode(const daikin_state_t *s, const daikin_profile_t *p,
                   uint8_t out[DAIKIN_FRAME_BYTES]) {
  memset(out, 0, DAIKIN_FRAME_BYTES);

  /* Section 1: identity / comfort frame. */
  out[0] = 0x11;
  out[1] = 0xDA;
  out[2] = 0x27;
  out[3] = 0x00;
  out[4] = 0xC5;
  out[5] = p->id_byte5;
  out[6] = s->comfort ? 0x10 : 0x00;
  out[7] = daikin_section_sum(out, 7);

  /* Section 2: wall clock. */
  out[8] = 0x11;
  out[9] = 0xDA;
  out[10] = 0x27;
  out[11] = 0x00;
  out[12] = 0x42;
  out[13] = (uint8_t)(s->clock_min & 0xFF);
  out[14] = (uint8_t)(((s->clock_min >> 8) & 0x07) | ((s->clock_day & 0x07) << 3));
  out[15] = daikin_section_sum(out + 8, 7);

  /* Section 3: settings. */
  out[16] = 0x11;
  out[17] = 0xDA;
  out[18] = 0x27;
  out[19] = 0x00;
  out[20] = 0x00;
  out[21] = (uint8_t)((s->power ? 0x01 : 0x00) | (s->on_timer ? 0x02 : 0x00) |
                      (s->off_timer ? 0x04 : 0x00) | 0x08 |
                      (((uint8_t)s->mode & 0x07) << 4));
  if (s->mode == DAIKIN_MODE_DRY && p->dry_temp_marker) {
    out[22] = 0xC0;
  } else if (s->mode == DAIKIN_MODE_FAN && p->fan_temp25) {
    out[22] = 50;
  } else {
    out[22] = s->temp_half_deg;
  }
  out[23] = 0x00;
  out[24] = (uint8_t)((((uint8_t)s->fan & 0x0F) << 4) | (s->swing_v ? 0x0F : 0x00));
  out[25] = s->swing_h ? 0x0F : 0x00;
  {
    uint16_t on = s->on_timer ? (uint16_t)(s->on_timer_min & 0x0FFF) : DAIKIN_TIMER_DISABLED;
    uint16_t off = s->off_timer ? (uint16_t)(s->off_timer_min & 0x0FFF) : DAIKIN_TIMER_DISABLED;
    out[26] = (uint8_t)(on & 0xFF);
    out[27] = (uint8_t)(((on >> 8) & 0x0F) | ((off & 0x0F) << 4));
    out[28] = (uint8_t)((off >> 4) & 0xFF);
  }
  out[29] = (uint8_t)((s->powerful ? 0x01 : 0x00) | (s->quiet ? 0x20 : 0x00));
  out[30] = 0x00;
  out[31] = p->fixed_byte31;
  out[32] = (uint8_t)((s->sensor ? 0x02 : 0x00) | (s->econo ? 0x04 : 0x00) |
                      (s->weekly_timer_off ? 0x80 : 0x00));
  out[33] = (uint8_t)(p->fixed_byte33 | (s->mold ? 0x02 : 0x00));
  out[34] = daikin_section_sum(out + 16, 18);
}

bool daikin_valid_frame(const uint8_t f[DAIKIN_FRAME_BYTES]) {
  static const uint8_t header[3] = {0x11, 0xDA, 0x27};
  static const size_t section_start[3] = {0, 8, 16};
  for (int i = 0; i < 3; i++) {
    if (memcmp(f + section_start[i], header, 3) != 0) return false;
  }
  if (f[4] != 0xC5 || f[12] != 0x42 || f[20] != 0x00) return false;
  if (f[7] != daikin_section_sum(f, 7)) return false;
  if (f[15] != daikin_section_sum(f + 8, 7)) return false;
  if (f[34] != daikin_section_sum(f + 16, 18)) return false;
  return true;
}

bool daikin_decode(const uint8_t f[DAIKIN_FRAME_BYTES], daikin_state_t *s) {
  if (!daikin_valid_frame(f)) return false;
  daikin_state_init(s);
  s->comfort = (f[6] & 0x10) != 0;
  s->clock_min = (uint16_t)(f[13] | ((f[14] & 0x07) << 8));
  s->clock_day = (uint8_t)((f[14] >> 3) & 0x07);
  s->power = (f[21] & 0x01) != 0;
  s->on_timer = (f[21] & 0x02) != 0;
  s->off_timer = (f[21] & 0x04) != 0;
  s->mode = (daikin_mode_t)((f[21] >> 4) & 0x07);
  if (f[22] != 0xC0) s->temp_half_deg = f[22];
  s->fan = (daikin_fan_t)(f[24] >> 4);
  s->swing_v = (f[24] & 0x0F) != 0;
  s->swing_h = (f[25] & 0x0F) != 0;
  {
    uint16_t on = (uint16_t)(f[26] | ((f[27] & 0x0F) << 8));
    uint16_t off = (uint16_t)((f[27] >> 4) | (f[28] << 4));
    s->on_timer_min = s->on_timer ? on : 0;
    s->off_timer_min = s->off_timer ? off : 0;
  }
  s->powerful = (f[29] & 0x01) != 0;
  s->quiet = (f[29] & 0x20) != 0;
  s->sensor = (f[32] & 0x02) != 0;
  s->econo = (f[32] & 0x04) != 0;
  s->weekly_timer_off = (f[32] & 0x80) != 0;
  s->mold = (f[33] & 0x02) != 0;
  return valid_mode((uint8_t)s->mode) && valid_fan((uint8_t)s->fan);
}

size_t daikin_to_timings(const uint8_t f[DAIKIN_FRAME_BYTES],
                         const daikin_timing_t *t, uint32_t *out,
                         size_t max_out) {
  static const size_t section_start[3] = {0, 8, 16};
  static const size_t section_len[3] = {8, 8, 19};
  size_t n = 0;
  if (max_out < DAIKIN_TIMING_COUNT) return 0;

  /* Leader: five zero bits, a closing mark, then a long gap. */
  for (int i = 0; i < 5; i++) {
    out[n++] = t->leader_mark;
    out[n++] = t->leader_space;
  }
  out[n++] = t->leader_mark;
  out[n++] = t->leader_gap;

  for (int sec = 0; sec < 3; sec++) {
    out[n++] = t->hdr_mark;
    out[n++] = t->hdr_space;
    for (size_t i = 0; i < section_len[sec]; i++) {
      uint8_t byte = f[section_start[sec] + i];
      for (int bit = 0; bit < 8; bit++) { /* LSB first */
        out[n++] = t->bit_mark;
        out[n++] = ((byte >> bit) & 1) ? t->one_space : t->zero_space;
      }
    }
    out[n++] = t->bit_mark;    /* trailing mark closes the last bit */
    out[n++] = t->section_gap; /* gap before the next section / end */
  }
  return n;
}

const char *daikin_mode_name(daikin_mode_t m) {
  switch (m) {
    case DAIKIN_MODE_AUTO: return "AUTO";
    case DAIKIN_MODE_DRY: return "DRY";
    case DAIKIN_MODE_COOL: return "COOL";
    case DAIKIN_MODE_HEAT: return "HEAT";
    case DAIKIN_MODE_FAN: return "FAN";
    default: return "?";
  }
}

const char *daikin_fan_name(daikin_fan_t f) {
  switch (f) {
    case DAIKIN_FAN_AUTO: return "AUTO";
    case DAIKIN_FAN_QUIET: return "QUIET";
    case DAIKIN_FAN_1: return "1";
    case DAIKIN_FAN_2: return "2";
    case DAIKIN_FAN_3: return "3";
    case DAIKIN_FAN_4: return "4";
    case DAIKIN_FAN_5: return "5";
    default: return "?";
  }
}

const char *daikin_day_name(uint8_t day) {
  static const char *const names[8] = {"---", "Sun", "Mon", "Tue",
                                       "Wed", "Thu", "Fri", "Sat"};
  return day <= 7 ? names[day] : "???";
}

bool daikin_parse_mode(const char *text, daikin_mode_t *out) {
  if (equals_ignore_case(text, "auto")) { *out = DAIKIN_MODE_AUTO; return true; }
  if (equals_ignore_case(text, "dry")) { *out = DAIKIN_MODE_DRY; return true; }
  if (equals_ignore_case(text, "cool")) { *out = DAIKIN_MODE_COOL; return true; }
  if (equals_ignore_case(text, "heat")) { *out = DAIKIN_MODE_HEAT; return true; }
  if (equals_ignore_case(text, "fan")) { *out = DAIKIN_MODE_FAN; return true; }
  return false;
}

bool daikin_parse_fan(const char *text, daikin_fan_t *out) {
  if (equals_ignore_case(text, "auto")) { *out = DAIKIN_FAN_AUTO; return true; }
  if (equals_ignore_case(text, "quiet") || equals_ignore_case(text, "night")) {
    *out = DAIKIN_FAN_QUIET;
    return true;
  }
  if (text[0] >= '1' && text[0] <= '5' && text[1] == '\0') {
    *out = (daikin_fan_t)(2 + (text[0] - '0'));
    return true;
  }
  return false;
}
