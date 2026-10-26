#include "ui.h"

#include "board_config.h"

namespace {

constexpr int kHeaderH = 20;
constexpr int kFooterH = 14;
constexpr uint16_t kBgColor = TFT_BLACK;
constexpr uint16_t kHeaderColor = 0x1082;   // very dark blue-grey
constexpr uint16_t kDimColor = 0x7BEF;      // grey for "off" items
constexpr uint16_t kSelBg = TFT_YELLOW;
constexpr uint16_t kAccent = 0x5D9F;        // light blue

// Mode order of the ARC466A41 (cooling-only remote). Build with
// -D REMOTE_HEAT_PUMP=1 to cycle through all five modes.
#if REMOTE_HEAT_PUMP
const daikin_mode_t kModeCycle[] = {DAIKIN_MODE_AUTO, DAIKIN_MODE_DRY, DAIKIN_MODE_COOL,
                                    DAIKIN_MODE_HEAT, DAIKIN_MODE_FAN};
#else
const daikin_mode_t kModeCycle[] = {DAIKIN_MODE_DRY, DAIKIN_MODE_COOL, DAIKIN_MODE_FAN};
#endif
constexpr int kModeCount = sizeof(kModeCycle) / sizeof(kModeCycle[0]);

const daikin_fan_t kFanCycle[] = {DAIKIN_FAN_AUTO, DAIKIN_FAN_QUIET, DAIKIN_FAN_1, DAIKIN_FAN_2,
                                  DAIKIN_FAN_3,    DAIKIN_FAN_4,     DAIKIN_FAN_5};
constexpr int kFanCount = sizeof(kFanCycle) / sizeof(kFanCycle[0]);

// The A41's set-point range in COOL: 18.0 - 32.0 C.
constexpr uint8_t kUiMinTempHalf = 36;
constexpr uint8_t kUiMaxTempHalf = 64;

template <typename T>
int indexOf(const T* list, int count, T value) {
  for (int i = 0; i < count; i++)
    if (list[i] == value) return i;
  return 0;
}

}  // namespace

void Ui::begin() {
  spr_.setColorDepth(16);
  haveSprite_ = spr_.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  dirty_ = true;
}

TFT_eSPI* Ui::gfx() { return haveSprite_ ? static_cast<TFT_eSPI*>(&spr_) : &tft_; }

void Ui::nextField() {
  field_ = static_cast<Field>((field_ + 1) % kFieldCount);
  dirty_ = true;
}

void Ui::adjust(int dir) {
  daikin_state_t& s = ac_.state();
  switch (field_) {
    case kPower:
      s.power = !s.power;
      break;
    case kMode: {
      const int i = indexOf(kModeCycle, kModeCount, s.mode);
      s.mode = kModeCycle[(i + dir + kModeCount) % kModeCount];
      break;
    }
    case kTemp: {
      int t = s.temp_half_deg + dir;
      if (t < kUiMinTempHalf) t = kUiMinTempHalf;
      if (t > kUiMaxTempHalf) t = kUiMaxTempHalf;
      s.temp_half_deg = static_cast<uint8_t>(t);
      break;
    }
    case kFan: {
      const int i = indexOf(kFanCycle, kFanCount, s.fan);
      s.fan = kFanCycle[(i + dir + kFanCount) % kFanCount];
      break;
    }
    case kSwingV: s.swing_v = !s.swing_v; break;
    case kSwingH: s.swing_h = !s.swing_h; break;
    case kPowerful:
      s.powerful = !s.powerful;
      if (s.powerful) s.econo = s.quiet = false;
      break;
    case kEcono:
      s.econo = !s.econo;
      if (s.econo) s.powerful = false;
      break;
    case kQuiet:
      s.quiet = !s.quiet;
      if (s.quiet) s.powerful = false;
      break;
    case kSensor: s.sensor = !s.sensor; break;
    case kComfort: s.comfort = !s.comfort; break;
    default: break;
  }
  ac_.markChanged();
  dirty_ = true;
}

void Ui::flash(const char* msg, uint16_t color) {
  flashMsg_ = msg;
  flashColor_ = color;
  flashUntil_ = millis() + 1200;
  dirty_ = true;
}

int Ui::tokenWidth(const char* text, int font) { return gfx()->textWidth(text, font) + 6; }

void Ui::token(int x, int y, const char* text, bool selected, bool on, int font) {
  TFT_eSPI* g = gfx();
  const int w = g->textWidth(text, font) + 6;
  const int h = g->fontHeight(font);
  if (selected) {
    g->fillRoundRect(x, y - 1, w, h + 2, 3, kSelBg);
    g->setTextColor(TFT_BLACK, kSelBg);
  } else {
    g->setTextColor(on ? TFT_WHITE : kDimColor, kBgColor);
  }
  g->setTextDatum(TL_DATUM);
  g->drawString(text, x + 3, y, font);
}

void Ui::loop() {
  const uint32_t now = millis();
  if (flashUntil_ && now > flashUntil_) {
    flashUntil_ = 0;
    dirty_ = true;
  }
  const uint16_t minute = ac_.clock().isSet() ? ac_.clock().minutes() : 0xFFFE;
  if (minute != lastMinute_) {
    lastMinute_ = minute;
    dirty_ = true;
  }
  if (ac_.pendingSend() != lastPending_) {
    lastPending_ = ac_.pendingSend();
    dirty_ = true;
  }
  if (dirty_) {
    draw();
    dirty_ = false;
  }
}

void Ui::draw() {
  TFT_eSPI* g = gfx();
  const daikin_state_t& s = ac_.state();
  const int W = SCREEN_W;
  const int H = SCREEN_H;

  g->fillRect(0, 0, W, H, kBgColor);

  // ---- header -----------------------------------------------------------
  g->fillRect(0, 0, W, kHeaderH, kHeaderColor);
  g->setTextColor(kAccent, kHeaderColor);
  g->setTextDatum(TL_DATUM);
  g->drawString("DAIKIN ARC466A41", 4, 2, 2);
  char clock[24];
  if (ac_.clock().isSet()) {
    snprintf(clock, sizeof(clock), "%02u:%02u %s", ac_.clock().minutes() / 60,
             ac_.clock().minutes() % 60, daikin_day_name(ac_.clock().day()));
  } else {
    snprintf(clock, sizeof(clock), "clock unset");
  }
  g->setTextDatum(TR_DATUM);
  g->setTextColor(ac_.clock().isSet() ? TFT_WHITE : kDimColor, kHeaderColor);
  g->drawString(clock, W - 4, 2, 2);

  // ---- left block: power, temperature, mode ----------------------------
  const int leftX = 4;
  int y = kHeaderH + 4;
  token(leftX, y, s.power ? "ON" : "OFF", field_ == kPower, s.power, 2);
  if (!s.power) {
    g->setTextColor(TFT_RED, kBgColor);
    g->setTextDatum(TL_DATUM);
    g->drawString("standby", leftX + tokenWidth("OFF", 2) + 4, y, 2);
  }
  y += 20;

  char temp[8];
  snprintf(temp, sizeof(temp), "%u.%u", s.temp_half_deg / 2, (s.temp_half_deg % 2) * 5);
  const bool tempShown = s.mode != DAIKIN_MODE_FAN;
  const int tempFont = (H >= 170) ? 7 : 6;
  const int tempW = g->textWidth(temp, tempFont);
  const int tempH = g->fontHeight(tempFont);
  if (field_ == kTemp) g->fillRoundRect(leftX, y - 2, tempW + 8, tempH + 4, 4, kSelBg);
  g->setTextColor(field_ == kTemp ? TFT_BLACK : (tempShown && s.power ? TFT_WHITE : kDimColor),
                  field_ == kTemp ? kSelBg : kBgColor);
  g->setTextDatum(TL_DATUM);
  g->drawString(tempShown ? temp : "--.-", leftX + 4, y, tempFont);
  // Degree sign and unit.
  const int unitX = leftX + tempW + 14;
  g->drawCircle(unitX, y + 6, 3, tempShown ? TFT_WHITE : kDimColor);
  g->setTextColor(tempShown ? TFT_WHITE : kDimColor, kBgColor);
  g->drawString("C", unitX + 6, y + 2, 4);
  y += tempH + 6;

  token(leftX, y, daikin_mode_name(s.mode), field_ == kMode, true, 4);

  // ---- right block ----------------------------------------------------
  const int rightX = W * 11 / 20;
  const int rows = 5;
  const int rowH = (H - kHeaderH - kFooterH - 4) / rows;
  int ry = kHeaderH + 3;
  char fan[16];
  snprintf(fan, sizeof(fan), "FAN %s", daikin_fan_name(s.fan));
  token(rightX, ry, fan, field_ == kFan, true, 2);
  ry += rowH;

  g->setTextColor(TFT_WHITE, kBgColor);
  g->setTextDatum(TL_DATUM);
  g->drawString("SWING", rightX + 3, ry, 2);
  int x = rightX + tokenWidth("SWING", 2) + 2;
  token(x, ry, "V", field_ == kSwingV, s.swing_v, 2);
  x += tokenWidth("V", 2) + 4;
  token(x, ry, "H", field_ == kSwingH, s.swing_h, 2);
  ry += rowH;

  token(rightX, ry, "PWRFUL", field_ == kPowerful, s.powerful, 2);
  token(rightX + tokenWidth("PWRFUL", 2) + 6, ry, "ECONO", field_ == kEcono, s.econo, 2);
  ry += rowH;
  token(rightX, ry, "QUIET", field_ == kQuiet, s.quiet, 2);
  token(rightX + tokenWidth("PWRFUL", 2) + 6, ry, "EYE", field_ == kSensor, s.sensor, 2);
  ry += rowH;
  token(rightX, ry, "COMFORT", field_ == kComfort, s.comfort, 2);
  if (s.on_timer || s.off_timer) {
    g->setTextColor(kAccent, kBgColor);
    g->setTextDatum(TL_DATUM);
    g->drawString("TIMER", rightX + tokenWidth("COMFORT", 2) + 6, ry, 2);
  }

  // ---- footer ---------------------------------------------------------
  const int fy = H - kFooterH + 2;
  g->setTextDatum(TL_DATUM);
  if (flashUntil_) {
    g->setTextColor(flashColor_, kBgColor);
    g->drawString(flashMsg_, 4, fy, 1);
  } else if (ac_.pendingSend()) {
    g->setTextColor(TFT_ORANGE, kBgColor);
    g->drawString("sending...", 4, fy, 1);
  } else {
    g->setTextColor(kDimColor, kBgColor);
    g->drawString("L:next  R:+  hold R:-  hold L:send", 4, fy, 1);
  }
  if (!ac_.irReady()) {
    g->setTextDatum(TR_DATUM);
    g->setTextColor(TFT_RED, kBgColor);
    g->drawString("IR FAIL", W - 4, fy, 1);
  }

  if (haveSprite_) spr_.pushSprite(0, 0);
}
