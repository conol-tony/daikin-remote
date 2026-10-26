#include "commands.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr int kMaxTokens = 6;

int tokenize(const String& line, char* buf, size_t bufLen, const char* tokens[kMaxTokens]) {
  strncpy(buf, line.c_str(), bufLen - 1);
  buf[bufLen - 1] = '\0';
  int n = 0;
  char* p = buf;
  while (*p && n < kMaxTokens) {
    while (*p == ' ' || *p == '\t' || *p == ',' || *p == '=') p++;
    if (!*p) break;
    tokens[n++] = p;
    while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != '=') {
      *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
      p++;
    }
    if (*p) *p++ = '\0';
  }
  return n;
}

bool parseOnOff(const char* t, bool& out) {
  if (!strcmp(t, "on") || !strcmp(t, "1") || !strcmp(t, "true") || !strcmp(t, "yes")) {
    out = true;
    return true;
  }
  if (!strcmp(t, "off") || !strcmp(t, "0") || !strcmp(t, "false") || !strcmp(t, "no")) {
    out = false;
    return true;
  }
  return false;
}

bool parseHHMM(const char* t, uint16_t& minutes) {
  int h = 0, m = 0;
  if (sscanf(t, "%d:%d", &h, &m) != 2) return false;
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  minutes = static_cast<uint16_t>(h * 60 + m);
  return true;
}

bool parseDay(const char* t, uint8_t& day) {
  static const char* const kNames[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
  for (uint8_t i = 0; i < 7; i++) {
    if (!strncmp(t, kNames[i], 3)) {
      day = i + 1;
      return true;
    }
  }
  if (t[0] >= '0' && t[0] <= '7' && t[1] == '\0') {
    day = t[0] - '0';
    return true;
  }
  return false;
}

// Accepts "24", "24.5", "24,5".
bool parseTemp(const char* t, uint8_t& halfDeg) {
  char* end = nullptr;
  double v = strtod(t, &end);
  if (end == t) return false;
  if (*end == ',' && end[1] >= '0' && end[1] <= '9') v += (end[1] - '0') / 10.0;
  if (v < 5 || v > 40) return false;
  halfDeg = static_cast<uint8_t>(static_cast<int>(v * 2 + 0.5));
  return true;
}

bool parseHexFrame(const char* const tokens[], int count, uint8_t out[DAIKIN_FRAME_BYTES]) {
  // Accept either one 70-char hex string or 35 separate bytes.
  String joined;
  for (int i = 0; i < count; i++) joined += tokens[i];
  if (joined.length() != DAIKIN_FRAME_BYTES * 2) return false;
  for (size_t i = 0; i < DAIKIN_FRAME_BYTES; i++) {
    char b[3] = {joined[2 * i], joined[2 * i + 1], '\0'};
    char* end = nullptr;
    const long v = strtol(b, &end, 16);
    if (*end != '\0') return false;
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

}  // namespace

void CommandHandler::ok(const char* what) {
  console_.printf("OK %s | %s\n", what, ac_.describe().c_str());
}

void CommandHandler::err(const char* what) { console_.printf("ERR %s\n", what); }

void CommandHandler::printRaw() {
  uint8_t frame[DAIKIN_FRAME_BYTES];
  ac_.buildFrame(frame);
  String hex;
  for (size_t i = 0; i < DAIKIN_FRAME_BYTES; i++) {
    char b[4];
    snprintf(b, sizeof(b), "%02X", frame[i]);
    hex += b;
    if (i == 7 || i == 15) hex += " | ";
    else if (i + 1 < DAIKIN_FRAME_BYTES) hex += " ";
  }
  console_.printf("FRAME %s\n", hex.c_str());
}

void CommandHandler::help() {
  console_.print(
      "Daikin ARC466A41 emulator commands:\n"
      "  on | off | power on|off|toggle\n"
      "  mode auto|dry|cool|heat|fan\n"
      "  temp <10..32 in 0.5 steps> | temp + | temp -\n"
      "  fan auto|quiet|1|2|3|4|5\n"
      "  swing v on|off | swing h on|off | swing both|off\n"
      "  powerful on|off   econo on|off   quiet on|off (outdoor unit)\n"
      "  sensor on|off     comfort on|off   mold on|off   weekly on|off\n"
      "  timer on HH:MM | timer on off     timer off HH:MM | timer off off\n"
      "  clock HH:MM [sun..sat]            set the clock stamped into frames\n"
      "  send                              transmit now\n"
      "  autosend on|off                   transmit automatically after changes\n"
      "  status | json | raw               show state / JSON / the 35-byte frame\n"
      "  sendraw <35 hex bytes>            transmit an arbitrary frame\n"
      "  reset                             defaults (off, cool, 25.0, fan auto)\n");
}

bool CommandHandler::handle(const String& line) {
  char buf[160];
  const char* t[kMaxTokens] = {nullptr};
  const int n = tokenize(line, buf, sizeof(buf), t);
  if (n == 0) return false;
  daikin_state_t& s = ac_.state();
  const char* cmd = t[0];
  bool flag = false;

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    help();
    return false;
  }
  if (!strcmp(cmd, "status")) {
    console_.println(ac_.describe());
    return false;
  }
  if (!strcmp(cmd, "json")) {
    console_.println(ac_.toJson());
    return false;
  }
  if (!strcmp(cmd, "raw")) {
    printRaw();
    return false;
  }
  if (!strcmp(cmd, "send")) {
    if (ac_.send()) {
      printRaw();
      ok("sent");
    } else {
      err("IR transmitter not ready");
    }
    return false;
  }
  if (!strcmp(cmd, "sendraw")) {
    uint8_t frame[DAIKIN_FRAME_BYTES];
    if (n < 2 || !parseHexFrame(t + 1, n - 1, frame)) {
      err("expected 35 hex bytes");
      return false;
    }
    if (!daikin_valid_frame(frame)) console_.print("WARN frame checksums/headers invalid, sending anyway\n");
    console_.printf(ac_.sendRaw(frame) ? "OK raw frame sent\n" : "ERR IR transmitter not ready\n");
    return false;
  }
  if (!strcmp(cmd, "autosend")) {
    if (n < 2 || !parseOnOff(t[1], flag)) {
      err("autosend on|off");
      return false;
    }
    ac_.setAutoSend(flag);
    ok("autosend");
    return false;
  }
  if (!strcmp(cmd, "reset")) {
    ac_.resetDefaults();
    ok("reset");
    return true;
  }
  if (!strcmp(cmd, "clock")) {
    uint16_t minutes;
    if (n < 2 || !parseHHMM(t[1], minutes)) {
      err("clock HH:MM [sun..sat]");
      return false;
    }
    uint8_t day = ac_.clock().day();
    if (n >= 3 && !parseDay(t[2], day)) {
      err("day must be sun..sat");
      return false;
    }
    ac_.clock().set(minutes, day);
    ok("clock");
    return false;
  }

  // --- state-changing commands ---
  if (!strcmp(cmd, "on")) {
    s.power = true;
  } else if (!strcmp(cmd, "off")) {
    s.power = false;
  } else if (!strcmp(cmd, "power")) {
    if (n >= 2 && !strcmp(t[1], "toggle")) {
      s.power = !s.power;
    } else if (n < 2 || !parseOnOff(t[1], s.power)) {
      err("power on|off|toggle");
      return false;
    }
  } else if (!strcmp(cmd, "mode")) {
    if (n < 2 || !daikin_parse_mode(t[1], &s.mode)) {
      err("mode auto|dry|cool|heat|fan");
      return false;
    }
  } else if (!strcmp(cmd, "temp")) {
    if (n < 2) {
      err("temp <value>|+|-");
      return false;
    }
    if (!strcmp(t[1], "+") || !strcmp(t[1], "up")) {
      s.temp_half_deg++;
    } else if (!strcmp(t[1], "-") || !strcmp(t[1], "down")) {
      if (s.temp_half_deg > 0) s.temp_half_deg--;
    } else if (!parseTemp(t[1], s.temp_half_deg)) {
      err("temp must be 10..32");
      return false;
    }
  } else if (!strcmp(cmd, "fan")) {
    if (n < 2 || !daikin_parse_fan(t[1], &s.fan)) {
      err("fan auto|quiet|1..5");
      return false;
    }
  } else if (!strcmp(cmd, "swing")) {
    if (n >= 2 && !strcmp(t[1], "both")) {
      s.swing_v = s.swing_h = true;
    } else if (n >= 2 && !strcmp(t[1], "off")) {
      s.swing_v = s.swing_h = false;
    } else if (n >= 3 && (!strcmp(t[1], "v") || !strcmp(t[1], "vertical")) && parseOnOff(t[2], flag)) {
      s.swing_v = flag;
    } else if (n >= 3 && (!strcmp(t[1], "h") || !strcmp(t[1], "horizontal")) && parseOnOff(t[2], flag)) {
      s.swing_h = flag;
    } else {
      err("swing v|h on|off, swing both, swing off");
      return false;
    }
  } else if (!strcmp(cmd, "powerful") || !strcmp(cmd, "boost")) {
    if (n < 2 || !parseOnOff(t[1], s.powerful)) { err("powerful on|off"); return false; }
    if (s.powerful) { s.econo = false; s.quiet = false; }
  } else if (!strcmp(cmd, "econo") || !strcmp(cmd, "eco")) {
    if (n < 2 || !parseOnOff(t[1], s.econo)) { err("econo on|off"); return false; }
    if (s.econo) s.powerful = false;
  } else if (!strcmp(cmd, "quiet")) {
    if (n < 2 || !parseOnOff(t[1], s.quiet)) { err("quiet on|off"); return false; }
    if (s.quiet) s.powerful = false;
  } else if (!strcmp(cmd, "sensor") || !strcmp(cmd, "eye")) {
    if (n < 2 || !parseOnOff(t[1], s.sensor)) { err("sensor on|off"); return false; }
  } else if (!strcmp(cmd, "comfort")) {
    if (n < 2 || !parseOnOff(t[1], s.comfort)) { err("comfort on|off"); return false; }
  } else if (!strcmp(cmd, "mold") || !strcmp(cmd, "mould")) {
    if (n < 2 || !parseOnOff(t[1], s.mold)) { err("mold on|off"); return false; }
  } else if (!strcmp(cmd, "weekly")) {
    if (n < 2 || !parseOnOff(t[1], flag)) { err("weekly on|off"); return false; }
    s.weekly_timer_off = !flag;
  } else if (!strcmp(cmd, "timer")) {
    if (n < 3) { err("timer on|off HH:MM|off"); return false; }
    bool* enable = nullptr;
    uint16_t* minutes = nullptr;
    if (!strcmp(t[1], "on")) { enable = &s.on_timer; minutes = &s.on_timer_min; }
    else if (!strcmp(t[1], "off")) { enable = &s.off_timer; minutes = &s.off_timer_min; }
    else { err("timer on|off HH:MM|off"); return false; }
    if (!strcmp(t[2], "off") || !strcmp(t[2], "cancel")) {
      *enable = false;
    } else if (parseHHMM(t[2], *minutes)) {
      *enable = true;
    } else {
      err("time must be HH:MM");
      return false;
    }
  } else {
    err("unknown command, try 'help'");
    return false;
  }

  ac_.markChanged();
  ok(cmd);
  return true;
}
