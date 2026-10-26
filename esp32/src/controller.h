// Remote-control state machine: owns the Daikin state, the software wall
// clock the frames carry, persistence in NVS, and the IR transmitter.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "daikin_arc466.h"
#include "ir_tx.h"

// Minutes-since-midnight clock driven by millis(). The remote stamps every
// frame with its wall clock; the A/C copies it and uses it for timers.
class WallClock {
 public:
  void set(uint16_t minutes, uint8_t day) {
    baseMin_ = minutes % 1440;
    day_ = day > 7 ? 0 : day;
    baseMs_ = millis();
    set_ = true;
  }
  bool isSet() const { return set_; }
  uint16_t minutes() const { return static_cast<uint16_t>(total() % 1440); }
  uint8_t day() const {
    if (day_ == 0) return 0;
    return static_cast<uint8_t>(((day_ - 1 + total() / 1440) % 7) + 1);
  }

 private:
  uint32_t total() const { return baseMin_ + (millis() - baseMs_) / 60000UL; }
  uint16_t baseMin_ = 0;
  uint8_t day_ = 0;
  uint32_t baseMs_ = 0;
  bool set_ = false;
};

class AcController {
 public:
  // Delay between the last change and the automatic transmission.
  static constexpr uint32_t kAutoSendDelayMs = 900;

  bool begin(int irPin);

  daikin_state_t& state() { return state_; }
  const daikin_state_t& state() const { return state_; }
  WallClock& clock() { return clock_; }
  const WallClock& clock() const { return clock_; }

  // Call after editing state(): schedules auto-send and persistence.
  void markChanged();
  // Transmit the current state now. Returns false if the IR driver failed.
  bool send();
  // Transmit an arbitrary 35-byte frame as-is (no checksum fix-up).
  bool sendRaw(const uint8_t frame[DAIKIN_FRAME_BYTES]);
  // Service auto-send and deferred persistence. Call from loop().
  void loop();

  void setAutoSend(bool on) { autoSend_ = on; }
  bool autoSend() const { return autoSend_; }
  bool pendingSend() const { return dirty_ && autoSend_; }
  bool irReady() const { return ir_.ready(); }

  // Encode the current state (with the live clock) into a frame.
  void buildFrame(uint8_t out[DAIKIN_FRAME_BYTES]);
  const uint8_t* lastFrame() const { return lastFrame_; }
  bool everSent() const { return everSent_; }
  uint32_t lastSentMs() const { return lastSentMs_; }
  uint32_t sendCount() const { return sendCount_; }

  void resetDefaults();
  String describe() const;
  String toJson() const;

 private:
  struct Persisted {
    uint8_t version;
    daikin_state_t state;
    bool autoSend;
  };
  static constexpr uint8_t kPersistVersion = 2;

  void persist();
  void load();

  daikin_state_t state_;
  IrTransmitter ir_;
  WallClock clock_;
  Preferences prefs_;
  bool autoSend_ = true;
  bool dirty_ = false;
  bool persistDirty_ = false;
  uint32_t dirtyMs_ = 0;
  uint8_t lastFrame_[DAIKIN_FRAME_BYTES] = {0};
  uint32_t lastSentMs_ = 0;
  uint32_t sendCount_ = 0;
  bool everSent_ = false;
};
