# LilyGo T-Display hardware notes for the Daikin ARC466A41 IR emulator

Research snapshot: 2026-10-12. `HW/` denotes downloaded source snapshots used
for the original investigation; those files and local build logs are not
distributed with this repository. Public upstream links appear alongside the
findings. The maintained build settings are in
[`esp32/platformio.ini`](../esp32/platformio.ini), and the primary SF32 target
is documented in the [project README](../README.md).

---

## 0. Identifying a T-Display board from its USB interface

### 0a. What the ID means

WCH assigns USB VID:PID `1a86:55d3` to the CH343 and `1a86:55d4` to the
CH9102. Linux's CDC-ACM driver supports the CH343; a serial device commonly
appears as `/dev/ttyACM0`. The generic USB product string alone does not identify
the MCU behind the bridge. See the [Linux CDC-ACM device table](https://github.com/torvalds/linux/blob/master/drivers/usb/class/cdc-acm.c)
and [WCH CH343 driver](https://github.com/WCHSoftGroup/ch343ser_linux).

### 0b. T-Display family vs USB bridge

| Board | MCU / display | USB bridge | USB VID:PID | Evidence |
|---|---|---|---|---|
| **TTGO T-Display (classic, 1.14")** | ESP32-D0WDQ6, ST7789V 135×240 SPI, no PSRAM, 4 MB (16 MB option) | **CP2104** (2019) → **CH9102F** (V1.1+); CH340K clones exist | 10c4:ea60 (CP2104) / **1a86:55d4** (CH9102F) | Schematic `HW/TTGO_T-Display_schematic_ESP32-TFT_6-26.pdf` (2019/6/26) block "USB转串口" `U4 CP2014` [sic]; product page "Serial chip: CH9102"; `HW/arduino_variant_lilygo_t_display_pins_arduino.h:6-7` `#define USB_VID 0x1A86` / `#define USB_PID 0x55D4`; `HW/pio_lilygo-t-display.json` `"hwids": [["0x1A86","0x55D4"]]`; `HW/ttgo_issue_125.json` "TTGO T-Display v1.1 ... CH9102F" |
| **T-Display-S3 / S3-Touch** | ESP32-S3R8 (8 MB OPI PSRAM, 16 MB flash), ST7789V 170×320 8-bit i80 | **none — native USB-Serial/JTAG** | **303a:1001** | Schematic `HW/T_Display_S3_schematic.pdf` (2024/4/29): USB-C DP/DN → `ESP_USB_DP`/`ESP_USB_DM` straight into `U2 ESP32_S3R8`, no bridge in BOM; `HW/arduino_variant_lilygo_t_display_s3_pins_arduino.h:6-7` `USB_VID 0x303a` / `USB_PID 0x1001`; README `HW/tdisplay_s3_README.md:110-112` "Upload Mode UART0/Hardware CDC", "USB Mode CDC and JTAG" |
| T-Display-S3-AMOLED (+1.43/1.64/1.75/Plus), S3-Long, S3-Pro, Bar | ESP32-S3 | native USB | 303a:1001 | `HW/family_readmes/T-Display-S3-AMOLED_README.md:25`, `T-Display-S3-Long_README.md:69`, `T-Display-S3-Pro_README.md:96`, `T-Display-Bar_README.md:57` ("USB CDC On Boot Enable") |
| T-Display-C5 | ESP32-C5 | native USB-Serial-JTAG | 303a | `HW/family_readmes/T-Display-C5_readme.md:16` |
| T-Display-P4 | ESP32-P4 + ESP32-C6 | native (P4U port) | 303a | `HW/family_readmes/T-Display-P4_README.md:178-201` |
| **T-Display-SF32 ("T-SF32-Display")** | **SiFli SF32LB52x (Cortex-M33) + optional ESP32-C6 on keyboard board**, 2.16" CO5300 AMOLED | **CH343P** | **1a86:55d3** | [LilyGo SF32 documentation](https://github.com/Xinyuan-LilyGO/T-Display-SF32): USB-C data reaches the CH343P UART bridge rather than the SF32 USB peripheral |
| T-Display RP2040 / GD32 / S2 / K230 / Keyboard / CM0 | non-ESP32 or ESP32-S2 | n/a | — | `HW/family_readmes/` |

### 0c. Choosing the firmware target

The T-Display-SF32 is documented with a CH343P bridge and uses the SiFli SDK
and `sftool`. The classic T-Display and T-Display-S3 use the ESP32 environments
in this repository. Check the board marking and display size before choosing a
build; a USB bridge ID identifies the bridge, not the attached processor.

The SF32's IR emitter is on its keyboard expansion board and is driven through
PA36. On the ESP32 boards, an external emitter is needed. For an ESP32 bridge
revision whose USB ID is absent from PlatformIO's board definition, specify the
serial port explicitly when uploading.

---

## 1. Pin maps

### 1a. TTGO T-Display (classic ESP32)

The classic board connects its ST7789 display over SPI: MOSI 19, clock 18,
chip-select 5, data/command 16, reset 23 and backlight 4. It has no separate
MISO connection. I2C uses SDA 21 and SCL 22; the buttons use 35 and 0; battery
measurement uses ADC 34 with its divider enabled by GPIO14. Sources:
[LilyGo board documentation](https://github.com/Xinyuan-LilyGO/TTGO-T-Display)
and [TFT_eSPI Setup25](https://github.com/Bodmer/TFT_eSPI/blob/V2.5.43/User_Setups/Setup25_TTGO_T_Display.h).

`HW/ttgo_tdisplay_FactoryTest.ino:13-24`: `ADC_EN 14 //ADC_EN is the ADC detection enable port`, `ADC_PIN 34`, `BUTTON_1 35`,
`BUTTON_2 0`; battery `((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0)` (`:88`) after `digitalWrite(ADC_EN, HIGH)` (`:184`).
Arduino variant (`HW/arduino_variant_lilygo_t_display_pins_arduino.h:12-21, 54-58`): `TX 1, RX 3, SDA 21, SCL 22, SS 5, MOSI 23,
MISO 19, SCK 18, VBAT 34, RIGHT_BUTTON 35, LEFT_BUTTON 0` — the variant's generic VSPI `MOSI = 23 / MISO = 19` is **not** the
display wiring (display MOSI = 19, RST = 23); TFT_eSPI Setup25 is authoritative.

Full GPIO usage:

| GPIO | On-board use | Header | Notes |
|---|---|---|---|
| 0 | BUTTON2 (left of USB-C), boot strap | — | strapping |
| 1 / 3 | UART0 TX / RX to the USB bridge | — | |
| 2 | — | left row | strapping (must not be pulled high at boot by external circuit); ADC12/TOUCH2 |
| 4 | TFT_BL backlight | — | |
| 5 | TFT_CS | — | strapping (on-board use only) |
| 6-11 | SPI flash | — | never use |
| 12 | — | left row | strapping (MTDI, must be LOW at boot) — avoid |
| 13 | — | left row | free; ADC14/TOUCH4 |
| 14 | ADC_EN (battery divider enable) | — | |
| 15 | — | left row | strapping (MTDO, pulled up internally) — usable with care |
| 16 | TFT_DC | — | |
| 17 | — | left row | **free** (no PSRAM on this board) |
| 18 | TFT_SCLK | — | |
| 19 | TFT_MOSI | — | |
| 21 | I2C SDA (10 k pull-up on board) | left row | free if I2C unused |
| 22 | I2C SCL (10 k pull-up on board) | left row | free if I2C unused |
| 23 | TFT_RST | — | |
| 25 / 26 | — | right row | free; DAC1 / DAC2, ADC18/19 |
| 27 | — | right row | **free**; ADC17/TOUCH7 |
| 32 / 33 | — | right row | free; ADC4/5, TOUCH9/8 |
| 34 | ADC_IN battery sense | — | input-only |
| 35 | BUTTON1 (right of USB-C) | — | input-only |
| 36, 37, 38, 39 | — | right row | **input-only**, no pull-ups (ESP-IDF GPIO doc); 270 pF caps between 36/37 and 38/39 on the schematic |

Header rows (`HW/ttgo_tdisplay_pinmap.jpg`, "ESP32 TTGO T-Display V1.1", schematic headers P4/P5):
left (top→bottom) `GND, GND, 21, 22, 17, 2, 15, 13, 12, GND, GND, 3V3`;
right `3V3, 36, 37, 38, 39, 32, 33, 25, 26, 27, GND, 5V`.

ESP32 rules (ESP-IDF GPIO doc, https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html):
"Strapping pin: GPIO0, GPIO2, GPIO5, GPIO12 (MTDI), and GPIO15 (MTDO)"; "GPIO6-11 and GPIO16-17 are usually connected to the
SPI flash and PSRAM" (no PSRAM here, so 17 is free and brought out); "GPIO34-39 can only be set as input mode".

### 1b. T-Display-S3 (ESP32-S3)

The S3 board uses an eight-bit parallel ST7789 interface. The data bus is on
GPIO39, 40, 41, 42, 45, 46, 47 and 48; control signals use reset 5, CS 6,
DC 7, WR 8 and RD 9. Backlight is GPIO38 and peripheral power is enabled by
GPIO15. Buttons use 0 and 14, while battery ADC uses 4. The touch variant
uses interrupt 16 and reset 21. See [LilyGo's factory pin definitions](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/examples/factory/pin_config.h).

Arduino variant (`HW/arduino_variant_lilygo_t_display_s3_pins_arduino.h:9-60`): `BUTTON_1 0, BUTTON_2 14, BAT_VOLT 4, TX 43, RX 44,
SDA 18, SCL 17, SS 10, MOSI 11, MISO 13, SCK 12, TP_RESET 21, TP_INIT 16, LCD_BL 38, LCD_D0..D7 = 39,40,41,42,45,46,47,48,
LCD_WR 8, LCD_RD 9, LCD_DC 7, LCD_CS 6, LCD_RES 5, LCD_POWER_ON 15`; header groups `P1: 43, 44, 18, 17, 21, 16` and `P2: 1, 2, 3, 10, 11, 12, 13`.

Full GPIO usage:

| GPIO | On-board use | Header | Notes |
|---|---|---|---|
| 0 | BOOT button | — | strapping |
| 1 | — | P2 | **free**; ADC1_CH0 / TOUCH1 |
| 2 | — | P2 | **free**; ADC1_CH1 / TOUCH2 |
| 3 | — | P2 | strapping, `R1 10K` pull-up on schematic — avoid as output |
| 4 | battery ADC (reads only when USB-C unplugged) | — | |
| 5 / 6 / 7 / 8 / 9 | LCD RES / CS / DC / WR / RD | — | |
| 10, 11, 12, 13 | — (SPI CS/D/CLK/Q labels; SD shield only) | P2 | **free** on the standard board |
| 14 | button 2 | — | |
| 15 | **peripheral power enable (must be HIGH)** | — | |
| 16 / 21 | touch INT / RES on Touch variant | P1 | free on non-touch boards |
| 17 / 18 | I2C SCL / SDA (Qwiic; 10 k pull-ups on Touch boards) | P1 | usable if I2C unused |
| 19 / 20 | USB D- / D+ | — | never use |
| 26-32 | flash | — | never use |
| 33-37 | OPI PSRAM (ESP32-S3R8) | — | never use |
| 38 | LCD backlight | — | |
| 39-42, 45-48 | LCD D0-D7 | — | 45/46 are also strapping |
| 43 / 44 | UART0 TX / RX (console when CDC is disabled) | P1 + JST-SH | avoid for IR |

Header rows (`HW/tdisplay_s3_pinmap.jpg`, silkscreen "T_Display_S3 22-5-18 V1.0"): P2 (left) `3V, 1, 2, 3, 10, 11, 12, 13, NC, NC, GND, 5V`;
P1 (right) `GND, GND, 43, 44, 18, 17, 21, 16, NC, GND, GND, 3V`; bottom JST-SH `G, 3V, 43, 44`.

README notes (`HW/tdisplay_s3_README.md:161-170`): "GPIO15 is the peripheral power control pin. You must set it HIGH before using
the board, otherwise the LCD and other peripherals will not work. ... `pinMode(15, OUTPUT); digitalWrite(15, HIGH);`";
"The standard T-Display-S3 board does not have an onboard SD card slot."; "The battery ADC pin can only read the battery voltage
when the USB-C port is not plugged in". Electrical (`:265-276`): USB-C 5 V; battery 3.7-4.2 V; VBUS header 5 V; VBAT header 4.2 V;
"3V pin header load does not exceed 100mA"; "The 5V pin header power source comes from USB-C".

ESP32-S3 rules (https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html):
"Strapping pin: GPIO0, GPIO3, GPIO45 and GPIO46"; "GPIO26 ~ GPIO32 are usually used for SPI flash and PSRAM"; "on boards embedded
with ESP32-S3R8 / ESP32-S3R8V chip, GPIO33 ~ GPIO37 are also not recommended"; "GPIO19 and GPIO20 are used by USB-JTAG by default".

---

## 2. Free GPIOs for the IR LED and the recommended default

| Board | Safe outputs on the header | **Recommended IR pin** | Avoid |
|---|---|---|---|
| Classic ESP32 T-Display | 17, 27, 25, 26, 32, 33, 13, (21, 22 if I2C unused; 10 k pull-ups there are harmless for an NPN base) | **GPIO17** (fallback GPIO27) | 0/35 buttons; 4/5/16/18/19/23 display; 34 ADC; 14 ADC_EN; 1/3 UART; 2/12/15 strapping (12 must stay LOW at boot); 36-39 input-only |
| T-Display-S3 | 1, 2, 10, 11, 12, 13 (P2 side, no board pull-ups); 16, 21 on non-touch boards; 17/18 if I2C unused | **GPIO1** (fallback GPIO2; GPIO21 on non-touch) | 3 strapping; 43/44 UART0; 0/14 buttons; 15 power enable; 19/20 USB; 26-37 flash/PSRAM; 38-42/45-48 LCD |

Both ESP32 and ESP32-S3 can route the RMT TX signal (and LEDC PWM) to any output-capable GPIO through the GPIO matrix, so the
choice is purely about avoiding on-board functions and strapping pins. Keep the IR drive transistor base pulled to GND (10 k) so
the LED cannot turn on while the pin floats during reset/boot.

---

## 3. Build identifiers

### 3a. PlatformIO board identifiers

| Target | PlatformIO board | MCU | Flash | Display interface |
|---|---|---|---|---|
| Classic T-Display | `lilygo-t-display` | ESP32 | 4 MB by default | SPI |
| T-Display-S3 | `lilygo-t-display-s3` | ESP32-S3 | 16 MB | Eight-bit parallel |

The pinned PlatformIO release's [classic board definition](https://github.com/platformio/platform-espressif32/blob/v6.5.0/boards/lilygo-t-display.json)
and [S3 board definition](https://github.com/platformio/platform-espressif32/blob/v6.5.0/boards/lilygo-t-display-s3.json)
provide the upload, flash and Arduino variant settings. The classic board has no
PSRAM; the S3 has 8 MB of OPI PSRAM. Board definitions can differ between
PlatformIO and pioarduino, so use this project's pinned environment when
reproducing its build.

### 3b. Platform and Arduino core

The project pins `espressif32@6.5.0`, which supplies Arduino-ESP32 2.0.14.
That matches the version used by [LilyGo's S3 PlatformIO configuration](https://github.com/Xinyuan-LilyGO/T-Display-S3/blob/main/platformio.ini).
LilyGo documents TFT_eSPI compatibility problems with newer Arduino cores;
[TFT_eSPI issue #3329](https://github.com/Bodmer/TFT_eSPI/issues/3329) records
the reported S3 boot failures. A move to Arduino core 3.x needs a separate
check of the display library and RMT API; it is not required for these builds.

### 3c. Arduino-ESP32 FQBNs (`HW/arduino_esp32_boards.txt`)

- **`esp32:esp32:lilygo_t_display`** — `lilygo_t_display.name=LilyGo T-Display` (line 6224); since core **2.0.12**.
  Key settings: `build.board=LILYGO_T_DISPLAY`, `build.flash_size=4MB`, `build.flash_freq=80m`, `build.flash_mode=dio`, `build.boot=dio`,
  `build.partitions=default`, `upload.speed=460800`, `upload.maximum_size=1310720`, **`serial.disableDTR=true`, `serial.disableRTS=true`**.
  Menus: `PSRAM.{disabled,enabled}`, `FlashSize.{4M,16M}`, `FlashMode.{qio,dio}`, `PartitionScheme.{default,...,huge_app,min_spiffs,...}`, `CPUFreq`, `UploadSpeed`, `DebugLevel`, `EraseFlash`.
  Example: `esp32:esp32:lilygo_t_display:PSRAM=disabled,FlashSize=4M,PartitionScheme=default`.
  LilyGo's alternative (`HW/ttgo_tdisplay_README.md:8`): "ESP32 Dev Module", PSRAM Disable, Flash 4MB (= `esp32:esp32:esp32`).
- **`esp32:esp32:lilygo_t_display_s3`** — `lilygo_t_display_s3.name=LilyGo T-Display-S3` (line 6373); since core **2.0.7**.
  Key settings (`HW/arduino_boards_txt_lilygo_t_display_s3_block.txt:10-45`): `build.board=LILYGO_T_DISPLAY_S3`, `build.usb_mode=1`,
  `build.cdc_on_boot=1`, `build.flash_size=16MB`, `build.flash_mode=dio`, `build.boot=qio`, `build.partitions=app3M_fat9M_16MB`,
  `build.psram_type=opi`, `build.memory_type={build.boot}_{build.psram_type}`, `upload.speed=921600`, `upload.maximum_size=3145728`.
  Menus: `USBMode.{hwcdc,default}` (hwcdc → "Hardware CDC and JTAG"; default → "USB-OTG (TinyUSB)"), `CDCOnBoot.{cdc,default}`,
  `PartitionScheme.{app3M_fat9M_16MB,rainmaker,rainmaker_4MB,rainmaker_8MB,tinyuf2,tinyuf2_noota}`, `JTAGAdapter`, `UploadMode`, `DebugLevel`, `EraseFlash`.
  Example: `esp32:esp32:lilygo_t_display_s3:USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB`.
  LilyGo README (`HW/tdisplay_s3_README.md:94-119`) alternative: "ESP32S3 Dev Module" with USB CDC On Boot = Enable, Flash 16MB,
  Partition "16M Flash (3M APP/9.9MB FATFS)", PSRAM "OPI PSRAM", Upload Mode "UART0/Hardware CDC", USB Mode "CDC and JTAG".

### 3d. Project configuration

Use [`esp32/platformio.ini`](../esp32/platformio.ini) for both maintained
ESP32 environments. It supplies the display configuration through build flags
and selects IR GPIO17 on the classic board or GPIO1 on the S3 through
[`board_config.h`](../esp32/src/board_config.h). The S3 firmware enables GPIO15
before initialising the display. The project uses its own Daikin encoder;
IRremoteESP8266 is a protocol reference, not a build dependency.

---

## 4. Display library setups

### 4a. Classic display settings

[TFT_eSPI Setup25](https://github.com/Bodmer/TFT_eSPI/blob/V2.5.43/User_Setups/Setup25_TTGO_T_Display.h)
selects the ST7789, a 135×240 panel, display RAM offsets and the SPI pins listed
in §1a. Its SPI write clock is 40 MHz and read clock is 6 MHz. The backlight
is active high. These settings are supplied by the project configuration;
library files do not need editing.

### 4b. S3 display settings

[TFT_eSPI Setup206](https://github.com/Bodmer/TFT_eSPI/blob/V2.5.43/User_Setups/Setup206_LilyGo_T_Display_S3.h)
selects the ST7789, a 170×320 panel, eight-bit parallel bus, RGB order,
inversion, RAM offsets and initialisation sequence 3. Its pins are listed in
§1b. GPIO15 must be high before display initialisation, and GPIO38 controls the
active-high backlight. Use `USER_SETUP_LOADED` with the project's build flags
to select these settings.

### 4c. Core-3.x-friendly alternatives

- **ESP-IDF `esp_lcd` i80 + LVGL** (LilyGo S3 factory example, `HW/tdisplay_s3_factory.ino:250-330`): `esp_lcd_new_i80_bus` with
  `.dc_gpio_num = 7, .wr_gpio_num = 8, .clk_src = LCD_CLK_SRC_PLL160M, data 39,40,41,42,45,46,47,48, bus_width 8`;
  `esp_lcd_new_panel_io_i80(... .cs_gpio_num = 6, .pclk_hz = 16000000, .lcd_cmd_bits = 8, .lcd_param_bits = 8 ...)`;
  `esp_lcd_new_panel_st7789(... .reset_gpio_num = 5, .bits_per_pixel = 16)`; `esp_lcd_panel_invert_color(true)`,
  `esp_lcd_panel_swap_xy(true)`, `esp_lcd_panel_mirror(false, true)`, `esp_lcd_panel_set_gap(0, 35)`; `digitalWrite(9 /*RD*/, HIGH)` and GPIO15 HIGH first.
- **Arduino_GFX** (`HW/tdisplay_s3_Arduino_GFXDemo.ino:6-16, 27-35`): `Arduino_ESP32PAR8Q(7 /*DC*/, 6 /*CS*/, 8 /*WR*/, 9 /*RD*/, 39, 40, 41, 42, 45, 46, 47, 48)`,
  `Arduino_ST7789(bus, 5 /*RST*/, 0, true /*IPS*/, 170, 320, 35, 0, 35, 0)`, `GFX_BL 38`, `GFX_EXTRA_PRE_INIT()` drives GPIO15 HIGH.
  LilyGo FAQ (`HW/tdisplay_s3_README.md:203`): run `Arduino_GFXDemo` first to separate hardware from TFT_eSPI problems.
- **LovyanGFX**: classic board auto-detects (`HW/lovyangfx_LGFX_AutoDetect_ESP32_all.hpp:2459-2496`: `board_TTGO_TDisplay`, ST7789,
  MOSI 19, SCLK 18, DC 16, CS 5, RST 23, `panel_width 135, panel_height 240, offset_x 52, offset_y 40, invert = true`, backlight PWM on GPIO4);
  **no S3 autodetect** — write an `LGFX` class with `Bus_Parallel8` (WR 8, RD 9, RS 7, D0-D7 as above) + `Panel_ST7789` (CS 6, RST 5, 170×320, offset_x 35, invert) and drive GPIO15.

---

## 5. IR LED drive circuit

**Neither board has an IR emitter** (schematic BOMs: MCU, flash, USB bridge/USB-C, TP4054/TP4065 charger, AP2112K LDO, buttons,
ST7789 FPC, AW9364DNR backlight driver on the S3, charge LED; no IR LED on either pin map). An external 940 nm LED and a
transistor are required; an ESP32 GPIO must not drive 100 mA directly (ESP-IDF drive strength is "~40 mA at most" per Espressif
staff; IRremoteESP8266 FAQ: "The ESP32's I/O pins are not 5V-tolerant").

**LED: Vishay TSAL6400** (`HW/TSAL6400_datasheet_extracted.txt`, doc 81011 Rev. 2.1, 2025-03-17): 940 nm GaAlAs, ±25°, T-1¾ 5 mm;
absolute max IF **100 mA**, IFM **200 mA** at tp = 100 µs / tp/T = 0.5, IFSM 1.5 A (100 µs), PV 160 mW, Tj 100 °C, RthJA 230 K/W;
VF **1.35 V typ / 1.6 V max** at IF = 100 mA; Ie 25 min / 50 typ mW/sr at 100 mA; Φe 40 mW.

**Recommended circuit (NPN low-side switch, LED fed from the 5 V / VBUS header pin):**
```
 5V header pin (VBUS; on the S3 "The 5V pin header power source comes from USB-C")
  |
 [R_LED] 33 Ω, ≥ 0.5 W     I_peak ≈ (5.0 − 1.35 − 0.2) / 33 ≈ 105 mA ; at 50 % carrier duty ≈ 52 mA average
  |                         P_R ≈ 0.37 W peak / 0.18 W average (a 1 W resistor runs cool)
  ▼  TSAL6400  anode → cathode
  |
  C
  B ──[R_B 1 kΩ]── ESP32 GPIO (3.3 V, 38 kHz-modulated mark/space)   I_B ≈ (3.3 − 0.7) / 1 k ≈ 2.6 mA → needs hFE ≥ ~40 at 100 mA
  E ── GND (common with the board)                                   (use 470 Ω for margin with a 2N3904)
 10 kΩ from B to GND so the LED stays off while the GPIO floats at reset/boot
```
- From the 3.3 V pin instead: R_LED = (3.3 − 1.35 − 0.2) / 0.1 ≈ 17.5 Ω → **18 Ω**, but the result is sensitive to VF spread
  (1.35-1.6 V → 70-100 mA) and the S3 README limits the 3V header load to 100 mA, so prefer VBUS. Note the 5 V pin is dead on
  battery power on the S3; use VBAT (4.2 V, R ≈ 27 Ω) or 3.3 V in that case.
- Transistors: 2N2222(A) (IC 800 mA), S8050 (hFE 85-300, IC 700 mA), 2N3904 (IC 200 mA, marginal hFE at 100 mA → 470 Ω base).
  A logic-level N-MOSFET (e.g. AO3400, 100 Ω gate resistor, 100 k gate pull-down) is equivalent.
- Pulse budget: the Daikin frame is ~450 ms of bursts (longest continuous mark 3.5 ms) at 38 kHz; 105 mA peak / ~52 mA average
  at 50 % duty is inside the 100 mA continuous rating with margin, and far below IFM 200 mA. For more range, 2 LEDs in series from 5 V
  with R ≈ 20 Ω still fits (5 − 2×1.35 − 0.2 = 2.1 V / 20 Ω ≈ 105 mA).
- Reference designs: IRremoteESP8266 wiki "NodeMCU with a 940nm IR LED and NPN transistor (e.g a 2N3904)" and the ESP01 circuit
  ("IR output directly connected to the base of a 2n2222 transistor ... IR LED anode is connected to VCC with 10-100 Ohm current limiting
  resistor"); esp32.co.uk IR-blaster guide ("~1 kΩ for transistor base", "~100-220 Ω for IR LED current limiting" from 3.3 V, i.e. a
  conservative 10-20 mA design — fine for a few metres line-of-sight; the 33 Ω/5 V design above is for 100 mA).
- Carrier generation on the ESP32: IRremoteESP8266 bit-bangs the carrier in software (`research/IRremoteESP8266/src/IRsend.cpp:164-207
  IRsend::mark` toggles the pin with busy-wait delays; `src/IRsend.h:33-35` "kPeriodOffset = -2 ... code excution time in producing the
  software PWM signal" for ESP32). For jitter-free output use the RMT peripheral (ESP-IDF `rmt_tx` with `rmt_carrier_config_t`
  frequency 38000, duty 0.33-0.5; 8 channels on ESP32, 4 TX on ESP32-S3) or ESPHome `remote_transmitter: pin: GPIOxx, carrier_duty_percent: 50%`
  ("ESP32s have a dedicated RMT hardware peripheral which ensures accurate signal timing"). Either way disable nothing on the display:
  RMT output is DMA/hardware-timed and unaffected by TFT drawing.

---

## 6. Board revision checks

Confirm the flash capacity of a classic T-Display before overriding the 4 MB
default. On an S3, confirm whether the touch controller occupies GPIO16 and
GPIO21. USB bridge substitutions on third-party boards can change enumeration
and upload-port detection without changing the display wiring.

## 7. Public source index

- [LilyGo TTGO T-Display](https://github.com/Xinyuan-LilyGO/TTGO-T-Display): classic board documentation, examples and schematics.
- [LilyGo T-Display-S3](https://github.com/Xinyuan-LilyGO/T-Display-S3): S3 board documentation, examples and schematics.
- [LilyGo T-Display-SF32](https://github.com/Xinyuan-LilyGO/T-Display-SF32): SF32 board and keyboard expansion documentation.
- [PlatformIO ESP32 platform v6.5.0](https://github.com/platformio/platform-espressif32/tree/v6.5.0): board definitions and pinned build platform.
- [TFT_eSPI v2.5.43](https://github.com/Bodmer/TFT_eSPI/tree/V2.5.43): display setup references.
- [Arduino-ESP32 board definitions](https://github.com/espressif/arduino-esp32/blob/2.0.14/boards.txt): Arduino board identifiers and settings.
- [ESP32 GPIO reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html) and [ESP32-S3 GPIO reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html): output and strapping constraints.
- [Vishay TSAL6400 datasheet](https://www.vishay.com/docs/81011/tsal6400.pdf): emitter ratings.

Historical `HW/` filenames elsewhere in this document identify the original
research snapshots. The source material remains with its upstream publishers.
