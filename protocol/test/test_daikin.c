/*
 * Host-side tests for the Daikin ARC466A41 encoder (C version).
 *
 * Golden frames come from:
 *  - 407 Broadlink captures of a real ARC466A41 (SmartIR PR #1021,
 *    codes/climate/1119.json), decoded in research/.
 *  - IRremoteESP8266 test/ir_Daikin_test.cpp (library defaults, a real
 *    capture by @sillyfrog, and the ARC452A4 weekly-timer frames from
 *    issue #704).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daikin_arc466.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    g_checks++;                                                  \
    if (!(cond)) {                                               \
      g_failures++;                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                            \
  } while (0)

static void hex(const uint8_t *b, size_t n, char *out) {
  size_t pos = 0;
  for (size_t i = 0; i < n; i++) {
    pos += (size_t)sprintf(out + pos, "%02X", b[i]);
    if (i == 7 || i == 15) pos += (size_t)sprintf(out + pos, " | ");
    else if (i + 1 < n) out[pos++] = ' ';
  }
  out[pos] = '\0';
}

static void expect_frame(const char *name, const daikin_state_t *st,
                         const daikin_profile_t *p,
                         const uint8_t want[DAIKIN_FRAME_BYTES]) {
  uint8_t got[DAIKIN_FRAME_BYTES];
  daikin_state_t s = *st;
  daikin_state_t back;
  daikin_sanitize(&s);
  daikin_encode(&s, p, got);
  g_checks++;
  if (memcmp(got, want, DAIKIN_FRAME_BYTES) != 0) {
    char a[160], b[160];
    g_failures++;
    hex(want, DAIKIN_FRAME_BYTES, a);
    hex(got, DAIKIN_FRAME_BYTES, b);
    printf("FAIL %s\n  want %s\n  got  %s\n", name, a, b);
  }
  CHECK(daikin_valid_frame(got));
  CHECK(daikin_decode(got, &back));
}

static void test_library_defaults(void) {
  /* IRDaikinESP::stateReset(): Power on, Heat, 15C, Fan Quiet, timers off. */
  daikin_state_t s, s2;
  const uint8_t want[DAIKIN_FRAME_BYTES] = {
      0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
      0x11, 0xDA, 0x27, 0x00, 0x42, 0x00, 0x00, 0x54,
      0x11, 0xDA, 0x27, 0x00, 0x00, 0x49, 0x1E, 0x00, 0xB0, 0x00,
      0x00, 0x06, 0x60, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x4F};
  const uint8_t want2[DAIKIN_FRAME_BYTES] = {
      0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
      0x11, 0xDA, 0x27, 0x00, 0x42, 0x00, 0x00, 0x54,
      0x11, 0xDA, 0x27, 0x00, 0x00, 0x48, 0x2A, 0x00, 0xB0, 0x00,
      0x00, 0x06, 0x60, 0x00, 0x00, 0xC0, 0x00, 0x02, 0x5C};
  daikin_state_init(&s);
  s.power = true;
  s.mode = DAIKIN_MODE_HEAT;
  s.temp_half_deg = 30;
  s.fan = DAIKIN_FAN_QUIET;
  expect_frame("library stateReset", &s, &DAIKIN_PROFILE_LIBRARY, want);

  /* TestDaikinClass.SetAndGetRaw: power toggled off, 21C, mould on. */
  s2 = s;
  s2.power = false;
  s2.temp_half_deg = 42;
  s2.mold = true;
  expect_frame("library SetAndGetRaw", &s2, &DAIKIN_PROFILE_LIBRARY, want2);
}

static void test_real_capture_sillyfrog(void) {
  /* TestDecodeDaikin.RealExample: Cool 29C fan auto, Powerful on, clock
   * 22:18, On timer 21:30, Off timer 06:10. */
  daikin_state_t s;
  const uint8_t want[DAIKIN_FRAME_BYTES] = {
      0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
      0x11, 0xDA, 0x27, 0x00, 0x42, 0x3A, 0x05, 0x93,
      0x11, 0xDA, 0x27, 0x00, 0x00, 0x3F, 0x3A, 0x00, 0xA0, 0x00,
      0x0A, 0x25, 0x17, 0x01, 0x00, 0xC0, 0x00, 0x00, 0x32};
  daikin_state_init(&s);
  s.power = true;
  s.mode = DAIKIN_MODE_COOL;
  s.temp_half_deg = 58;
  s.fan = DAIKIN_FAN_AUTO;
  s.powerful = true;
  s.clock_min = 22 * 60 + 18;
  s.clock_day = 0;
  s.on_timer = true;
  s.on_timer_min = 21 * 60 + 30;
  s.off_timer = true;
  s.off_timer_min = 6 * 60 + 10;
  expect_frame("sillyfrog real capture", &s, &DAIKIN_PROFILE_LIBRARY, want);
}

static void test_arc452a4_weekly_timer(void) {
  /* Issue #704: ARC452A4 + FTXS25LVMA, power off, Fan mode, fan 1,
   * clock 16:35 Sunday, with and without the weekly timer. */
  const daikin_profile_t arc452 = {0x00, 0xC1, 0x00, false, false};
  daikin_state_t s;
  const uint8_t want_off[DAIKIN_FRAME_BYTES] = {
      0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
      0x11, 0xDA, 0x27, 0x00, 0x42, 0xE3, 0x0B, 0x42,
      0x11, 0xDA, 0x27, 0x00, 0x00, 0x68, 0x32, 0x00, 0x30, 0x00,
      0x00, 0x06, 0x60, 0x00, 0x00, 0xC1, 0x80, 0x00, 0x83};
  const uint8_t want_on[DAIKIN_FRAME_BYTES] = {
      0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
      0x11, 0xDA, 0x27, 0x00, 0x42, 0xE3, 0x0B, 0x42,
      0x11, 0xDA, 0x27, 0x00, 0x00, 0x68, 0x32, 0x00, 0x30, 0x00,
      0x00, 0x06, 0x60, 0x00, 0x00, 0xC1, 0x00, 0x00, 0x03};
  daikin_state_init(&s);
  s.power = false;
  s.mode = DAIKIN_MODE_FAN;
  s.temp_half_deg = 50;
  s.fan = DAIKIN_FAN_1;
  s.clock_min = 16 * 60 + 35;
  s.clock_day = 1;
  s.weekly_timer_off = true;
  expect_frame("ARC452A4 weekly timer off", &s, &arc452, want_off);
  s.weekly_timer_off = false;
  expect_frame("ARC452A4 weekly timer on", &s, &arc452, want_on);
}

static void test_arc466a41_captures(void) {
  /* All captures were taken on a Friday (day 6) between 13:44 and 14:11. */
  daikin_state_t base, s;
  uint8_t got[DAIKIN_FRAME_BYTES];
  daikin_state_init(&base);
  base.power = true;
  base.mode = DAIKIN_MODE_COOL;
  base.temp_half_deg = 50;
  base.fan = DAIKIN_FAN_AUTO;
  base.clock_day = 6;

  {
    const uint8_t want[DAIKIN_FRAME_BYTES] = {
        0x11, 0xDA, 0x27, 0x00, 0xC5, 0x10, 0x00, 0xE7,
        0x11, 0xDA, 0x27, 0x00, 0x42, 0x39, 0x33, 0xC0,
        0x11, 0xDA, 0x27, 0x00, 0x00, 0x39, 0x32, 0x00, 0xA0, 0x00,
        0x00, 0x06, 0x60, 0x00, 0x00, 0xC5, 0x00, 0x08, 0x50};
    s = base; /* cool / fan auto / 25.0C, clock 13:45 */
    s.clock_min = 13 * 60 + 45;
    expect_frame("A41 cool/auto/25", &s, &DAIKIN_PROFILE_ARC466A41, want);
  }
  {
    const uint8_t want[DAIKIN_FRAME_BYTES] = {
        0x11, 0xDA, 0x27, 0x00, 0xC5, 0x10, 0x00, 0xE7,
        0x11, 0xDA, 0x27, 0x00, 0x42, 0x38, 0x33, 0xBF,
        0x11, 0xDA, 0x27, 0x00, 0x00, 0x39, 0x32, 0x00, 0xB0, 0x00,
        0x00, 0x06, 0x60, 0x00, 0x00, 0xC5, 0x00, 0x08, 0x60};
    s = base; /* cool / fan quiet / 25.0C, clock 13:44 */
    s.fan = DAIKIN_FAN_QUIET;
    s.clock_min = 13 * 60 + 44;
    expect_frame("A41 cool/quiet/25", &s, &DAIKIN_PROFILE_ARC466A41, want);
  }
  {
    const uint8_t want[DAIKIN_FRAME_BYTES] = {
        0x11, 0xDA, 0x27, 0x00, 0xC5, 0x10, 0x00, 0xE7,
        0x11, 0xDA, 0x27, 0x00, 0x42, 0x3F, 0x33, 0xC6,
        0x11, 0xDA, 0x27, 0x00, 0x00, 0x39, 0x24, 0x00, 0x70, 0x00,
        0x00, 0x06, 0x60, 0x00, 0x00, 0xC5, 0x00, 0x08, 0x12};
    s = base; /* cool / fan 5 / 18.0C, clock 13:51 */
    s.fan = DAIKIN_FAN_5;
    s.temp_half_deg = 36;
    s.clock_min = 13 * 60 + 51;
    expect_frame("A41 cool/level5/18", &s, &DAIKIN_PROFILE_ARC466A41, want);
  }
  {
    const uint8_t want[DAIKIN_FRAME_BYTES] = {
        0x11, 0xDA, 0x27, 0x00, 0xC5, 0x10, 0x00, 0xE7,
        0x11, 0xDA, 0x27, 0x00, 0x42, 0x53, 0x33, 0xDA,
        0x11, 0xDA, 0x27, 0x00, 0x00, 0x69, 0x32, 0x00, 0xA0, 0x00,
        0x00, 0x06, 0x60, 0x00, 0x00, 0xC5, 0x00, 0x08, 0x80};
    s = base; /* fan only / fan auto (temp byte forced to 25C), 14:11 */
    s.mode = DAIKIN_MODE_FAN;
    s.temp_half_deg = 36; /* must not leak into the frame */
    s.clock_min = 14 * 60 + 11;
    expect_frame("A41 fan_only/auto", &s, &DAIKIN_PROFILE_ARC466A41, want);
  }
  {
    const uint8_t want[DAIKIN_FRAME_BYTES] = {
        0x11, 0xDA, 0x27, 0x00, 0xC5, 0x10, 0x00, 0xE7,
        0x11, 0xDA, 0x27, 0x00, 0x42, 0x52, 0x33, 0xD9,
        0x11, 0xDA, 0x27, 0x00, 0x00, 0x68, 0x32, 0x00, 0xB0, 0x00,
        0x00, 0x06, 0x60, 0x00, 0x00, 0xC5, 0x00, 0x08, 0x8F};
    s = base; /* off (last mode fan, fan quiet), clock 14:10 */
    s.power = false;
    s.mode = DAIKIN_MODE_FAN;
    s.fan = DAIKIN_FAN_QUIET;
    s.clock_min = 14 * 60 + 10;
    expect_frame("A41 off", &s, &DAIKIN_PROFILE_ARC466A41, want);
  }
  /* Half-degree set-point: 18.5C -> 0x25. */
  s = base;
  s.temp_half_deg = 37;
  s.clock_min = 13 * 60 + 45;
  daikin_encode(&s, &DAIKIN_PROFILE_ARC466A41, got);
  CHECK(got[22] == 0x25);
  CHECK(daikin_valid_frame(got));
}

static void test_timings_against_library_string(void) {
  /* TestSendDaikin.SendDataOnly: raw bytes (their section 2/3 checksums
   * are deliberately not recomputed) and the exact mark/space string
   * IRremoteESP8266 emits for them. */
  const uint8_t raw[DAIKIN_FRAME_BYTES] = {
      0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
      0x11, 0xDA, 0x27, 0xF0, 0x00, 0x00, 0x00, 0x20,
      0x11, 0xDA, 0x27, 0x00, 0x00, 0x41, 0x1E, 0x00, 0xB0, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0xE3};
  const char *want =
      "m428s428m428s428m428s428m428s428m428s428"
      "m428s29428"
      "m3650s1623"
      "m428s1280m428s428m428s428m428s428m428s1280m428s428m428s428m428s428"
      "m428s428m428s1280m428s428m428s1280m428s1280m428s428m428s1280m428s1280"
      "m428s1280m428s1280m428s1280m428s428m428s428m428s1280m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s1280m428s428m428s1280m428s428m428s428m428s428m428s1280m428s1280"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s1280m428s1280m428s1280m428s428m428s1280m428s428m428s1280m428s1280"
      "m428s29428"
      "m3650s1623"
      "m428s1280m428s428m428s428m428s428m428s1280m428s428m428s428m428s428"
      "m428s428m428s1280m428s428m428s1280m428s1280m428s428m428s1280m428s1280"
      "m428s1280m428s1280m428s1280m428s428m428s428m428s1280m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s1280m428s1280m428s1280m428s1280"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s1280m428s428m428s428"
      "m428s29428"
      "m3650s1623"
      "m428s1280m428s428m428s428m428s428m428s1280m428s428m428s428m428s428"
      "m428s428m428s1280m428s428m428s1280m428s1280m428s428m428s1280m428s1280"
      "m428s1280m428s1280m428s1280m428s428m428s428m428s1280m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s1280m428s428m428s428m428s428m428s428m428s428m428s1280m428s428"
      "m428s428m428s1280m428s1280m428s1280m428s1280m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s1280m428s1280m428s428m428s1280"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s1280m428s1280"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s428m428s428m428s428m428s428m428s428m428s428m428s428m428s428"
      "m428s1280m428s1280m428s428m428s428m428s428m428s1280m428s1280m428s1280"
      "m428s29428";
  uint32_t t[DAIKIN_TIMING_COUNT];
  static char got[8192];
  size_t pos = 0;
  size_t n = daikin_to_timings(raw, &DAIKIN_TIMING_LIBRARY, t, DAIKIN_TIMING_COUNT);
  CHECK(n == DAIKIN_TIMING_COUNT);
  CHECK(n == 584);
  for (size_t i = 0; i < n; i++) {
    pos += (size_t)sprintf(got + pos, "%c%u", (i % 2 == 0) ? 'm' : 's', (unsigned)t[i]);
  }
  g_checks++;
  if (strcmp(got, want) != 0) {
    g_failures++;
    printf("FAIL timing string mismatch\n  want %.120s...\n  got  %.120s...\n", want, got);
  }
  CHECK(daikin_to_timings(raw, &DAIKIN_TIMING_LIBRARY, t, 10) == 0);
}

static void test_measured_timings_shape(void) {
  daikin_state_t s;
  uint8_t f[DAIKIN_FRAME_BYTES];
  uint32_t t[DAIKIN_TIMING_COUNT];
  uint32_t total = 0;
  size_t n;
  daikin_state_init(&s);
  s.power = true;
  daikin_encode(&s, &DAIKIN_PROFILE_ARC466A41, f);
  n = daikin_to_timings(f, &DAIKIN_TIMING_MEASURED, t, DAIKIN_TIMING_COUNT);
  CHECK(n == 584);
  for (int i = 0; i < 5; i++) {
    CHECK(t[2 * i] == 450);
    CHECK(t[2 * i + 1] == 410);
  }
  CHECK(t[10] == 450);
  CHECK(t[11] == 25150);
  CHECK(t[12] == 3480 && t[13] == 1740);
  /* Byte 0 = 0x11 LSB-first: 1,0,0,0,1,0,0,0. */
  CHECK(t[15] == 1285 && t[17] == 415 && t[19] == 415 && t[21] == 415);
  CHECK(t[23] == 1285 && t[25] == 415);
  /* End of section 1: index 12 + 2 + 128 = 142 is the trailing mark. */
  CHECK(t[142] == 450 && t[143] == 35550);
  CHECK(t[144] == 3480 && t[145] == 1740);
  /* Bit cells: '0' = 865 us, '1' = 1735 us, as measured on every receiver. */
  CHECK(t[14] + t[15] == 1735 && t[16] + t[17] == 865);
  for (size_t i = 0; i < n; i++) total += t[i];
  CHECK(total > 400000 && total < 700000);
}

static uint32_t lcg(uint32_t *x) {
  *x = *x * 1664525u + 1013904223u;
  return *x >> 8;
}

static void test_round_trip(void) {
  static const daikin_mode_t modes[] = {DAIKIN_MODE_AUTO, DAIKIN_MODE_DRY, DAIKIN_MODE_COOL,
                                        DAIKIN_MODE_HEAT, DAIKIN_MODE_FAN};
  static const daikin_fan_t fans[] = {DAIKIN_FAN_1, DAIKIN_FAN_2, DAIKIN_FAN_3, DAIKIN_FAN_4,
                                      DAIKIN_FAN_5, DAIKIN_FAN_AUTO, DAIKIN_FAN_QUIET};
  uint32_t seed = 12345;
  for (int i = 0; i < 2000; i++) {
    daikin_state_t s, back;
    uint8_t f[DAIKIN_FRAME_BYTES], g[DAIKIN_FRAME_BYTES], h[DAIKIN_FRAME_BYTES];
    daikin_state_init(&s);
    s.power = lcg(&seed) & 1;
    s.mode = modes[lcg(&seed) % 5];
    s.temp_half_deg = (uint8_t)(20 + lcg(&seed) % 45);
    s.fan = fans[lcg(&seed) % 7];
    s.swing_v = lcg(&seed) & 1;
    s.swing_h = lcg(&seed) & 1;
    s.powerful = lcg(&seed) & 1;
    s.quiet = lcg(&seed) & 1;
    s.econo = lcg(&seed) & 1;
    s.sensor = lcg(&seed) & 1;
    s.comfort = lcg(&seed) & 1;
    s.mold = lcg(&seed) & 1;
    s.weekly_timer_off = lcg(&seed) & 1;
    s.on_timer = lcg(&seed) & 1;
    s.on_timer_min = (uint16_t)(lcg(&seed) % 1440);
    s.off_timer = lcg(&seed) & 1;
    s.off_timer_min = (uint16_t)(lcg(&seed) % 1440);
    s.clock_min = (uint16_t)(lcg(&seed) % 1440);
    s.clock_day = (uint8_t)(lcg(&seed) % 8);
    daikin_sanitize(&s);

    /* Library profile carries the raw temperature in every mode. */
    daikin_encode(&s, &DAIKIN_PROFILE_LIBRARY, f);
    CHECK(daikin_valid_frame(f));
    CHECK(daikin_decode(f, &back));
    CHECK(back.power == s.power);
    CHECK(back.mode == s.mode);
    CHECK(back.temp_half_deg == s.temp_half_deg);
    CHECK(back.fan == s.fan);
    CHECK(back.swing_v == s.swing_v && back.swing_h == s.swing_h);
    CHECK(back.powerful == s.powerful && back.quiet == s.quiet);
    CHECK(back.econo == s.econo && back.sensor == s.sensor);
    CHECK(back.comfort == s.comfort && back.mold == s.mold);
    CHECK(back.weekly_timer_off == s.weekly_timer_off);
    CHECK(back.on_timer == s.on_timer);
    CHECK(!s.on_timer || back.on_timer_min == s.on_timer_min);
    CHECK(back.off_timer == s.off_timer);
    CHECK(!s.off_timer || back.off_timer_min == s.off_timer_min);
    CHECK(back.clock_min == s.clock_min && back.clock_day == s.clock_day);

    /* A41 profile: identical except the temperature byte in DRY / FAN. */
    daikin_encode(&s, &DAIKIN_PROFILE_ARC466A41, g);
    CHECK(daikin_valid_frame(g));
    CHECK(g[5] == 0x10 && g[31] == 0xC5 && (g[33] & 0x08) == 0x08);
    if (s.mode == DAIKIN_MODE_DRY) CHECK(g[22] == 0xC0);
    else if (s.mode == DAIKIN_MODE_FAN) CHECK(g[22] == 50);
    else CHECK(g[22] == s.temp_half_deg);
    /* Corrupting any byte must break validation. */
    memcpy(h, g, DAIKIN_FRAME_BYTES);
    h[lcg(&seed) % DAIKIN_FRAME_BYTES] ^= (uint8_t)(1 << (lcg(&seed) % 8));
    CHECK(!daikin_valid_frame(h));
  }
}

static void test_sanitize(void) {
  daikin_state_t s;
  daikin_state_init(&s);
  s.temp_half_deg = 5;
  s.powerful = true;
  s.econo = true;
  s.quiet = true;
  s.clock_min = 5000;
  s.clock_day = 9;
  daikin_sanitize(&s);
  CHECK(s.temp_half_deg == 20);
  CHECK(s.powerful && !s.econo && !s.quiet);
  CHECK(s.clock_min == 0 && s.clock_day == 0);
  s.temp_half_deg = 200;
  daikin_sanitize(&s);
  CHECK(s.temp_half_deg == 64);
}

static void test_names_and_parsing(void) {
  daikin_mode_t m;
  daikin_fan_t f;
  CHECK(daikin_parse_mode("cool", &m) && m == DAIKIN_MODE_COOL);
  CHECK(daikin_parse_mode("HEAT", &m) && m == DAIKIN_MODE_HEAT);
  CHECK(daikin_parse_mode("Dry", &m) && m == DAIKIN_MODE_DRY);
  CHECK(daikin_parse_mode("fan", &m) && m == DAIKIN_MODE_FAN);
  CHECK(daikin_parse_mode("auto", &m) && m == DAIKIN_MODE_AUTO);
  CHECK(!daikin_parse_mode("warm", &m));
  CHECK(daikin_parse_fan("auto", &f) && f == DAIKIN_FAN_AUTO);
  CHECK(daikin_parse_fan("quiet", &f) && f == DAIKIN_FAN_QUIET);
  CHECK(daikin_parse_fan("1", &f) && f == DAIKIN_FAN_1);
  CHECK(daikin_parse_fan("5", &f) && f == DAIKIN_FAN_5);
  CHECK(!daikin_parse_fan("6", &f));
  CHECK(!daikin_parse_fan("12", &f));
  CHECK(strcmp(daikin_mode_name(DAIKIN_MODE_COOL), "COOL") == 0);
  CHECK(strcmp(daikin_fan_name(DAIKIN_FAN_3), "3") == 0);
  CHECK(strcmp(daikin_day_name(1), "Sun") == 0 && strcmp(daikin_day_name(7), "Sat") == 0);
  CHECK(strcmp(daikin_day_name(0), "---") == 0);
}

int main(void) {
  test_library_defaults();
  test_real_capture_sillyfrog();
  test_arc452a4_weekly_timer();
  test_arc466a41_captures();
  test_timings_against_library_string();
  test_measured_timings_shape();
  test_round_trip();
  test_sanitize();
  test_names_and_parsing();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
