// Debounced push button with click / long-press / auto-repeat events.
#pragma once

#include <Arduino.h>

class Button {
 public:
  enum Event { kNone, kClick, kLongPress, kRepeat };

  explicit Button(int pin, bool internalPullup = true)
      : pin_(pin), pullup_(internalPullup) {}

  void begin() {
    pinMode(pin_, pullup_ ? INPUT_PULLUP : INPUT);
    stableDown_ = rawDown();
    lastRaw_ = stableDown_;
    lastChange_ = millis();
  }

  // Call from loop(). Returns at most one event per call.
  Event poll() {
    const uint32_t now = millis();
    const bool raw = rawDown();
    if (raw != lastRaw_) {
      lastRaw_ = raw;
      lastChange_ = now;
    }
    if (raw != stableDown_ && now - lastChange_ >= kDebounceMs) {
      stableDown_ = raw;
      if (stableDown_) {
        pressStart_ = now;
        longFired_ = false;
        nextRepeat_ = 0;
      } else if (!longFired_) {
        return kClick;
      }
    }
    if (stableDown_) {
      const uint32_t held = now - pressStart_;
      if (!longFired_ && held >= kLongMs) {
        longFired_ = true;
        nextRepeat_ = now + kRepeatMs;
        return kLongPress;
      }
      if (longFired_ && nextRepeat_ && now >= nextRepeat_) {
        nextRepeat_ = now + kRepeatMs;
        return kRepeat;
      }
    }
    return kNone;
  }

  bool isDown() const { return stableDown_; }

 private:
  static constexpr uint32_t kDebounceMs = 25;
  static constexpr uint32_t kLongMs = 650;
  static constexpr uint32_t kRepeatMs = 220;

  bool rawDown() const { return digitalRead(pin_) == LOW; }

  int pin_;
  bool pullup_;
  bool stableDown_ = false;
  bool lastRaw_ = false;
  bool longFired_ = false;
  uint32_t lastChange_ = 0;
  uint32_t pressStart_ = 0;
  uint32_t nextRepeat_ = 0;
};
