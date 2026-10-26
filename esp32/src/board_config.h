// Per-board pin assignments. Select the board with -D BOARD_TDISPLAY_S3 or
// -D BOARD_TDISPLAY (set by the PlatformIO env). Override PIN_IR_LED with
// -D PIN_IR_LED=<gpio> if you wire the IR LED driver somewhere else.
#pragma once

#if defined(BOARD_TDISPLAY_S3)
// LilyGo T-Display-S3: ESP32-S3R8, 1.9" ST7789 170x320 over 8-bit parallel.
#define BOARD_NAME "T-Display-S3"
#define PIN_BUTTON_1 0       // BOOT button, active low (external pull-up)
#define PIN_BUTTON_2 14      // KEY button, active low (needs internal pull-up)
#define PIN_LCD_POWER 15     // drive HIGH to enable the LCD power rail
#define PIN_LCD_BL 38        // backlight
#define PIN_BAT_ADC 4        // battery voltage divider
#ifndef PIN_IR_LED
#define PIN_IR_LED 2         // free header GPIO (left header: 1,2,3,10,11,12,13)
#endif
#define SCREEN_ROTATION 1    // landscape, USB on the left
#define SCREEN_W 320
#define SCREEN_H 170

#elif defined(BOARD_TDISPLAY)
// LilyGo/TTGO T-Display: ESP32, 1.14" ST7789 135x240 over SPI.
#define BOARD_NAME "T-Display"
#define PIN_BUTTON_1 0       // left button (BOOT), active low
#define PIN_BUTTON_2 35      // right button, active low, input-only pin
#define PIN_LCD_BL 4         // backlight
#define PIN_BAT_ADC 34       // battery voltage divider
#define PIN_ADC_EN 14        // HIGH enables the battery divider
#ifndef PIN_IR_LED
#define PIN_IR_LED 27        // free header GPIO
#endif
#define SCREEN_ROTATION 1    // landscape, USB on the left
#define SCREEN_W 240
#define SCREEN_H 135

#else
#error "Define BOARD_TDISPLAY_S3 or BOARD_TDISPLAY"
#endif

// Status LED is optional; T-Display boards have none.
#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED -1
#endif
