// Daikin ARC466A41 remote emulator for LilyGo T-Display boards.
//
// Controls: two on-board buttons (see ui.h) and a serial console
// (115200 baud, type "help"). Every change is transmitted automatically
// after a short delay, like a real remote.
#include <Arduino.h>
#include <TFT_eSPI.h>

#include "board_config.h"
#include "buttons.h"
#include "commands.h"
#include "console.h"
#include "controller.h"
#include "ui.h"

static TFT_eSPI tft;
static Console console;
static AcController ac;
static CommandHandler commands(ac, console);
static Ui ui(tft, ac);
static Button buttonLeft(PIN_BUTTON_1);
static Button buttonRight(PIN_BUTTON_2);

static void sendNow() {
  const bool ok = ac.send();
  ui.flash(ok ? "SENT" : "IR SEND FAILED", ok ? TFT_GREEN : TFT_RED);
  console.printf("%s %s\n", ok ? "TX" : "TX-FAILED", ac.describe().c_str());
}

void setup() {
  console.begin(115200);

#ifdef PIN_LCD_POWER
  pinMode(PIN_LCD_POWER, OUTPUT);
  digitalWrite(PIN_LCD_POWER, HIGH);
#endif
  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);
#ifdef PIN_LCD_BL
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
#endif

  buttonLeft.begin();
  buttonRight.begin();

  const bool irOk = ac.begin(PIN_IR_LED);
  ui.begin();

  console.printf("\nDaikin ARC466A41 emulator on %s, IR LED on GPIO %d (%s)\n", BOARD_NAME,
                 PIN_IR_LED, irOk ? "ready" : "INIT FAILED");
  console.printf("%s\n", ac.describe().c_str());
  console.print("Type 'help' for commands.\n");
}

void loop() {
  // Serial commands.
  String line;
  while (console.readLine(line)) {
    commands.handle(line);
    ui.invalidate();
  }

  // Buttons.
  switch (buttonLeft.poll()) {
    case Button::kClick: ui.nextField(); break;
    case Button::kLongPress: sendNow(); break;
    default: break;
  }
  switch (buttonRight.poll()) {
    case Button::kClick: ui.adjust(+1); break;
    case Button::kLongPress:
    case Button::kRepeat: ui.adjust(-1); break;
    default: break;
  }

  // Auto-send after changes settle.
  const bool wasPending = ac.pendingSend();
  ac.loop();
  if (wasPending && !ac.pendingSend()) {
    ui.flash("SENT", TFT_GREEN);
    console.printf("TX %s\n", ac.describe().c_str());
  }

  ui.loop();
  delay(5);
}
