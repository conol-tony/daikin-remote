#include "controller.h"

#include <string.h>

namespace {

String fmtTime(uint16_t minutes) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", minutes / 60, minutes % 60);
  return String(buf);
}

const char* onOff(bool v) { return v ? "on" : "off"; }
const char* boolStr(bool v) { return v ? "true" : "false"; }

}  // namespace

bool AcController::begin(int irPin) {
  daikin_state_init(&state_);
  prefs_.begin("daikin", false);
  load();
  return ir_.begin(irPin, DAIKIN_CARRIER_HZ, DAIKIN_CARRIER_DUTY_PERCENT);
}

void AcController::load() {
  Persisted p{};
  const size_t n = prefs_.getBytesLength("state");
  if (n != sizeof(Persisted)) return;
  if (prefs_.getBytes("state", &p, sizeof(p)) != sizeof(p)) return;
  if (p.version != kPersistVersion) return;
  state_ = p.state;
  autoSend_ = p.autoSend;
  // Timers and the clock are session-bound; do not resurrect them.
  state_.on_timer = false;
  state_.off_timer = false;
  state_.clock_min = 0;
  state_.clock_day = 0;
  daikin_sanitize(&state_);
}

void AcController::persist() {
  Persisted p{};
  p.version = kPersistVersion;
  p.state = state_;
  p.autoSend = autoSend_;
  prefs_.putBytes("state", &p, sizeof(p));
  persistDirty_ = false;
}

void AcController::markChanged() {
  daikin_sanitize(&state_);
  dirty_ = true;
  persistDirty_ = true;
  dirtyMs_ = millis();
}

void AcController::buildFrame(uint8_t out[DAIKIN_FRAME_BYTES]) {
  state_.clock_min = clock_.minutes();
  state_.clock_day = clock_.day();
  daikin_sanitize(&state_);
  daikin_encode(&state_, &DAIKIN_PROFILE_ARC466A41, out);
}

bool AcController::send() {
  uint8_t frame[DAIKIN_FRAME_BYTES];
  buildFrame(frame);
  dirty_ = false;
  const bool ok = sendRaw(frame);
  if (persistDirty_) persist();
  return ok;
}

bool AcController::sendRaw(const uint8_t frame[DAIKIN_FRAME_BYTES]) {
  static uint32_t timings[DAIKIN_TIMING_COUNT];
  const size_t n = daikin_to_timings(frame, &DAIKIN_TIMING_MEASURED, timings,
                                     DAIKIN_TIMING_COUNT);
  if (n == 0) return false;
  const bool ok = ir_.send(timings, n);
  memcpy(lastFrame_, frame, DAIKIN_FRAME_BYTES);
  lastSentMs_ = millis();
  everSent_ = true;
  sendCount_++;
  return ok;
}

void AcController::loop() {
  const uint32_t now = millis();
  if (dirty_ && autoSend_ && now - dirtyMs_ >= kAutoSendDelayMs) {
    send();
  } else if (persistDirty_ && now - dirtyMs_ >= 3000) {
    persist();
  }
}

void AcController::resetDefaults() {
  daikin_state_init(&state_);
  markChanged();
}

String AcController::describe() const {
  const daikin_state_t& s = state_;
  String out;
  out.reserve(220);
  out += "power=";
  out += onOff(s.power);
  out += " mode=";
  out += daikin_mode_name(s.mode);
  out += " temp=";
  out += String(s.temp_half_deg / 2);
  out += (s.temp_half_deg % 2) ? ".5" : ".0";
  out += " fan=";
  out += daikin_fan_name(s.fan);
  out += " swingV=";
  out += onOff(s.swing_v);
  out += " swingH=";
  out += onOff(s.swing_h);
  out += " powerful=";
  out += onOff(s.powerful);
  out += " econo=";
  out += onOff(s.econo);
  out += " quiet=";
  out += onOff(s.quiet);
  out += " sensor=";
  out += onOff(s.sensor);
  out += " comfort=";
  out += onOff(s.comfort);
  out += " onTimer=";
  out += s.on_timer ? fmtTime(s.on_timer_min) : "off";
  out += " offTimer=";
  out += s.off_timer ? fmtTime(s.off_timer_min) : "off";
  out += " clock=";
  out += clock_.isSet() ? fmtTime(clock_.minutes()) + " " + daikin_day_name(clock_.day())
                        : "unset";
  out += " autosend=";
  out += onOff(autoSend_);
  return out;
}

String AcController::toJson() const {
  const daikin_state_t& s = state_;
  String out;
  out.reserve(400);
  out += "{\"power\":";
  out += boolStr(s.power);
  out += ",\"mode\":\"";
  out += daikin_mode_name(s.mode);
  out += "\",\"temp\":";
  out += String(s.temp_half_deg / 2.0f, 1);
  out += ",\"fan\":\"";
  out += daikin_fan_name(s.fan);
  out += "\",\"swingV\":";
  out += boolStr(s.swing_v);
  out += ",\"swingH\":";
  out += boolStr(s.swing_h);
  out += ",\"powerful\":";
  out += boolStr(s.powerful);
  out += ",\"econo\":";
  out += boolStr(s.econo);
  out += ",\"quiet\":";
  out += boolStr(s.quiet);
  out += ",\"sensor\":";
  out += boolStr(s.sensor);
  out += ",\"comfort\":";
  out += boolStr(s.comfort);
  out += ",\"onTimer\":";
  out += s.on_timer ? "\"" + fmtTime(s.on_timer_min) + "\"" : "null";
  out += ",\"offTimer\":";
  out += s.off_timer ? "\"" + fmtTime(s.off_timer_min) + "\"" : "null";
  out += ",\"clock\":";
  out += clock_.isSet() ? "\"" + fmtTime(clock_.minutes()) + "\"" : "null";
  out += ",\"day\":";
  out += String(clock_.day());
  out += ",\"autosend\":";
  out += boolStr(autoSend_);
  out += ",\"sends\":";
  out += String(sendCount_);
  out += "}";
  return out;
}
