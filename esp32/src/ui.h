// Two-button display UI for the T-Display.
//   left button  click: move the cursor to the next field
//   left button  hold : transmit now
//   right button click: next value (temperature +0.5)
//   right button hold : previous value (temperature -0.5, auto-repeat)
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "controller.h"
#include "daikin_arc466.h"

class Ui {
 public:
  enum Field {
    kPower,
    kMode,
    kTemp,
    kFan,
    kSwingV,
    kSwingH,
    kPowerful,
    kEcono,
    kQuiet,
    kSensor,
    kComfort,
    kFieldCount
  };

  Ui(TFT_eSPI& tft, AcController& ac) : tft_(tft), spr_(&tft), ac_(ac) {}

  void begin();
  void nextField();
  // dir = +1 for the right button click, -1 for hold.
  void adjust(int dir);
  void flash(const char* msg, uint16_t color);
  void invalidate() { dirty_ = true; }
  void loop();

 private:
  void draw();
  TFT_eSPI* gfx();
  void token(int x, int y, const char* text, bool selected, bool on, int font);
  int tokenWidth(const char* text, int font);

  TFT_eSPI& tft_;
  TFT_eSprite spr_;
  AcController& ac_;
  bool haveSprite_ = false;
  Field field_ = kPower;
  bool dirty_ = true;
  uint32_t flashUntil_ = 0;
  String flashMsg_;
  uint16_t flashColor_ = TFT_GREEN;
  uint16_t lastMinute_ = 0xFFFF;
  bool lastPending_ = false;
};
