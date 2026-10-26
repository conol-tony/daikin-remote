# Daikin ARC466A41 remote emulator

Firmware that turns a LilyGo T-Display board into a Daikin air-conditioner
remote. It emulates the **ARC466A41** remote (the cooling-only remote for the
CTKM / CTKP wall units) bit-for-bit, from protocol research verified against
407 captures of a real ARC466A41.

The primary target is the **LilyGo T-Display-SF32** (SiFli SF32LB52X) with
the T-SF32-Keyboard expansion board. An ESP32 / ESP32-S3 T-Display variant
is also available in `esp32/`.

```
protocol/   platform-independent encoder (C99) + host tests   ./protocol/run_tests.sh
sf32/       T-Display-SF32 firmware (SiFli SDK, RT-Thread, LVGL)
esp32/      T-Display / T-Display-S3 firmware (PlatformIO, TFT_eSPI, RMT)
research/   protocol and hardware research notes
sdk/        external SiFli SDK and tools (not included)
```

## Host tests

The protocol and SF32 UI tests run without a board or vendor SDK. The
provided scripts target Linux and need Git, a POSIX shell, GCC, G++, and
a linker supporting `--gc-sections`.

```sh
git clone https://github.com/conol-tony/daikin-remote.git
cd daikin-remote
./protocol/run_tests.sh
./sf32/run_tests.sh
```

The protocol suite checks the encoder in C99 and C++17. The UI suite checks
keypad shortcuts, navigation, temperature limits, power and feature behavior
for both cooling-only and heat-pump configurations.

## Protocol summary

* IRremoteESP8266 `DAIKIN` family: 35 bytes = 280 data bits in three
  sections (8 / 8 / 19 bytes), each ending in an 8-bit byte sum. LSB first.
* 38 kHz carrier. Preamble of 5 zero bits + closing mark + 25.1 ms gap, then
  each section as 3.48 ms / 1.74 ms header, 450 µs marks with 415 µs ("0")
  or 1285 µs ("1") spaces, trailing mark, 35.5 ms gap.
* ARC466A41-specific constant bytes: byte 5 = 0x10, byte 31 = 0xC5,
  byte 33 = 0x08. Every frame carries the remote's clock (minutes + weekday).
* Fields: power, mode (dry / cool / fan; auto / heat exist in the protocol
  for heat-pump units), set-point 18.0–32.0 °C in 0.5 °C steps, fan
  auto / quiet / 1–5, swing V and H, Powerful, Econo, outdoor-unit Quiet,
  Intelligent-Eye sensor, Comfort airflow, mould, on/off timers, weekly
  timer enable.

`research/PROTOCOL.md` has the byte-by-byte table, timings, test vectors and
provenance. The encoder in `protocol/daikin_arc466.c` reproduces the real
captures and the IRremoteESP8266 golden frames exactly
(`./protocol/run_tests.sh`).

## T-Display-SF32 firmware (`sf32/`)

### Hardware

* IR output is **PA36** (GPTIM2 channel 1). The IR LED (VSMY14940 with an
  S8050 driver) is on the **T-SF32-Keyboard expansion board**, powered from
  its WIFI_3V3 rail, which the firmware switches on through the XL9555
  expander. Without the keyboard board, PA36 is on header CON1 pin 14; drive
  an external NPN + 940 nm LED from it (idle level is low).
* **The IR LED runs from the battery, not from USB.** WIFI_3V3 is an RT9080
  regulator fed from the battery net (VBAT / BAT_OUTSIDE), so a LiPo must be
  fitted and charged. The firmware configures the SGM41562B charger at boot
  (charging on, its I2C watchdog off); its power-on default is "charging
  disabled". Like LilyGo's firmware this puts the charger in battery-NTC
  mode: the cell needs a 10 kΩ NTC thermistor on pin 2 of the display
  board's 3-pin battery connector P2, as on LilyGo's stock pack. A 2-wire
  cell reads as "NTC cold" and is never charged.
* `ac status` shows `ir=... vbat=...mV charger=...`. The battery is measured
  at boot, before every send and once a minute; the boot log and every
  change of the charger state are printed on the console. The screen shows
  `NO IR POWER` (no battery, or WIFI_EN low), `WEAK IR` (battery below
  3.1 V), `IR POWER ?` (battery not measured) or `NO KEYBOARD` (PA36 only).
* On battery power below 3.55 V the screen shows `LOW BATTERY` and the board
  switches itself off 30 s later (charger shipping mode), as LilyGo asks
  application firmware to do. USB power or the D key turns it on again.
* **Aim the right-hand side of the keyboard shield at the AC.** The
  VSMY14940 is a side-looking emitter with a ±9° beam that leaves through
  the round "IR emitter hole" on the right side, near the bottom of the
  keypad. Pointing the top end at the unit like a TV remote misses the
  receiver. Keep fingers off the hole; up to about 7 m in direct line of
  sight.
* Room temperature, humidity and pressure come from the BME280 on the
  keyboard board (I2C1, address 0x76). It runs from the same battery-fed
  WIFI_3V3 rail as the IR LED, so it only answers with the keyboard board
  fitted and the battery switched on. See "Room sensor" below.
* Buttons: A = PA34, B = PA35, C = PA33. Display: 480x480 CO5300 AMOLED.
* Console: the USB-C port (CH343P) at **1000000 baud**, RT-Thread `msh`.

### Controls

The 20-key keyboard shield provides direct shortcuts. The screen shows the
digit beside each setting; the selected setting is highlighted in yellow.
Press display button A or keypad `ESC` to open or close a guide arranged
like the physical keypad. The screen footer shows `A help` and the common
shortcuts. A shortcut also closes the guide and selects the setting it changes.

| Keypad key | Action |
| --- | --- |
| `*` / `#` | temperature down / up by 0.5 °C (18–32 °C) |
| `0` | toggle AC power |
| `1` / `2` | cycle mode / fan speed |
| `3` | toggle Powerful |
| `4` / `5` | toggle vertical / horizontal swing |
| `6` / `7` | toggle Econo / outdoor-unit Quiet |
| `8` / `9` | toggle Intelligent Eye / Comfort airflow |
| `PREV` / `NEXT` | select previous / next field |
| `ENTER` / `DEL` | next / previous value of the selected field |
| `HOME` | select temperature and close the guide |
| `MAIL` (envelope) | transmit now |
| `STOP` | turn the AC off (also sends OFF again if already off) |
| `ESC` | open / close the key guide |

One press performs one action; releasing or holding a keypad key does not
repeat it. The keyboard needs the shield's battery-fed WIFI_3V3 supply.
The display buttons work alongside the keypad; if the keypad is unavailable
at boot, A selects the next field so all settings remain accessible:

| Input | Action |
| --- | --- |
| A click | open / close help; next field if keypad unavailable |
| A hold (0.8 s) | transmit now |
| B click / C click | next / previous value (temperature ±0.5 °C) |
| B hold (0.8 s) | previous field |
| C hold (0.8 s) | toggle power |

Every frame carries the full state including ON/OFF, like the real remote:
a change made while the screen shows OFF is sent as "OFF" (the unit beeps
but stays off). The status line says `SENT: ON` / `SENT: OFF`.

Every change is transmitted automatically about a second after the last
button press, like a real remote (`ac autosend off` disables that).
Navigation and opening the key guide do not transmit. `STOP` follows the
same auto-send setting; with auto-send off, press `MAIL` to transmit it.

Serial commands (type `ac help`):

```
ac on | off | power toggle
ac mode dry|cool|fan                ac temp 24.5 | temp + | temp -
ac fan auto|quiet|1..5              ac swing v on | swing h off | swing both
ac powerful on   ac econo on   ac quiet on   ac sensor on   ac comfort on
ac timer on 07:30   ac timer off off      ac clock 13:45 fri
ac send   ac status   ac json   ac raw   ac sendraw <35 hex bytes>   ac reset
ac env    ac env offset -1.5 [+3]   ac env offset reset
```

Like the remote, the console accepts 18.0–32.0 °C and no `auto` / `heat`
mode; build with `REMOTE_HEAT_PUMP` defined to allow those two.

The last state is saved on the flash filesystem and restored at boot. The
clock comes from the on-chip RTC; set it with `ac clock HH:MM day` (the SDK
re-initialises the RTC on a cold boot, so the frame carries clock 00:00 /
no weekday until it is set again; not yet confirmed on a CTKM unit).

Every transmission prints its duration measured two ways, e.g.
`ir_tx: 584 edges, 461010 us expected, 461010 us DWT, 461029 us LXT`. The
DWT figure always matches (same clock as the timing loop); the LXT figure
comes from the 32.768 kHz crystal and is flagged `TIMING ERROR` if it is off
by more than 2 %. None of this proves that light left the LED: check with a
phone camera (front camera, looking into the IR hole) during `ac send`.

### Room sensor (BME280)

The firmware reads the BME280 every 10 s in forced mode with ×1
oversampling and the IIR filter off, Bosch's "weather monitoring" setting.
The chip sleeps between samples, so it does not warm itself measurably.
The reading is shown on the main screen under the timer line
(`24.6°C  55%RH`, with `room  1008.3 hPa abs` below it, or `--` and
`room sensor: not found` / `read error` when there is no recent sample).
It is also in `ac status` (`env=ok tempC=24.6 rh=55.2 hPa=1008.3`) and in
`ac json` (`"tempC":24.6,"rh":55.2,"hPa":1008.3,"envStatus":"ok","envAgeS":3`,
with `null` values when there is no recent sample). The pressure is
absolute (station) pressure at the board, not the sea-level (QNH) value
that weather forecasts give; it reads about 12 hPa lower for every 100 m
of altitude. `ac env` takes a fresh
sample and prints the corrected and raw values, the dew point and the
sensor state:

```
env=ok tempC=24.6 rh=55.1 hPa=1008.3 dewC=15.0 age=0s offsetC=-1.5 offsetRh=+0.0
raw tempC=26.1 rh=50.4 (tempC + offsetC; rh recomputed for the same dew point, + offsetRh)
BME280 at 0x76 on i2c1: forced mode, oversampling x1, filter off, every 10 s; 42 samples, 0 failed
```

**It measures the board, not the room.** Bosch's data sheet says the
BME280's temperature "depends on the PCB temperature, sensor element
self-heating and ambient temperature and is typically above ambient
temperature". Here the display, the SoC, the charger (above all while it
charges over USB) and the battery warm the keyboard board. So expect it to
read a few degrees high and the humidity correspondingly low. Calibrate
against a reference thermometer next to the device, after it has run for
at least half an hour the way you normally use it (USB or battery, screen
on):

```
ac env offset -1.5        # add -1.5 °C to the temperature (RH offset unchanged)
ac env offset -1.5 +3     # and add +3 %RH after the correction
ac env offset reset
```

A temperature offset does not just shift the humidity. The firmware keeps
the measured water-vapour content (the dew point stays the same),
recomputes the relative humidity at the corrected temperature with the
Magnus formula, then adds the RH offset and clamps the result to 0–100 %.
The dew point is computed before that clamp, so it always stays the
measured one; if the corrected RH would exceed 100 % (a large negative
temperature offset at high humidity), `ac env` adds "note: temperature
offset puts the dew point above the room temperature".
The offsets (±10 °C, ±20 %RH) are saved in `/env_sensor.cfg` on the flash
filesystem. A calibration only holds for the conditions it was made in:
charging, a bright screen or sunlight on the board shift it.

If the sensor does not answer, the firmware probes again after 20 s, then
at doubling intervals up to every 5 minutes, at once when the battery or
WIFI_EN comes back, and on every `ac env`. Each address that does not
answer makes the SDK's I2C driver log two `drv.i2c` errors ("bus err" and
"reset and send 9 clks"). The first miss and every `ac env` try both 0x76
and 0x77, so they print four lines. The later probes try only 0x76 and
print two.

The SDK's `bme280` package and its RT-Thread sensor adapter are not used.
The adapter only runs the chip in normal mode (continuous sampling with
the IIR filter on), the driver returns I2C failures as valid readings, and
initialising it again after a failed probe trips an RT-Thread assertion.
`sf32/src/env_sensor.c` talks to the chip directly, with Bosch's reference
compensation code.

### SF32 build requirements

The provided setup targets **Linux x86_64**. Install Bash, Git, curl, tar,
xz, `sha256sum`, and uv, then run this from the repository root with network
access:

```sh
./sf32/setup-sdk.sh
```

The setup prepares a pinned **LilyGo SiFli SDK v2.4.1** tree in
`sdk/sifli-sdk`, with the `t-display-sf32_hcpu` board and keyboard-board
drivers. It restores the matching upstream build files, applies the
repository's Linux and charger fixes, installs Python 3.12 through uv and
the SDK's Python environment and Arm toolchain, and adds **sftool 0.2.5**
for flashing and backups. SDK sources and downloaded tools are excluded
from version control.

Setup refuses to overwrite an existing SDK directory. See
[`sdk-patches/README.md`](sdk-patches/README.md) for source pins, patch
provenance, and preparation options. The build and flash scripts source
`sf32/env.sh` automatically; a plain upstream SDK checkout does not replace
this board-specific setup.

### Build and flash

After preparing the SDK, connect the board with its battery installed and
select its serial port. `/dev/ttyACM0` below is an example; use the device
path assigned by your system and ensure your user can access it.

```sh
export SF32_PORT=/dev/ttyACM0
./sf32/build.sh                 # creates sf32/project/build_t-display-sf32_hcpu/
./sf32/backup.sh "$SF32_PORT"   # saves the regions that flashing overwrites
./sf32/flash.sh "$SF32_PORT"
```

The backup creates `sf32/backups/<timestamp>/` with flash chunks and a
`restore.sh` script. Backups are local files and are excluded from version
control. The backup script also requires GNU `timeout`.

Open the console at 1 Mbps and type `ac help`:

```sh
picocom -b 1000000 "$SF32_PORT"
```

Alternatively, use pyserial's terminal:

```sh
uv run --with pyserial python -m serial.tools.miniterm "$SF32_PORT" 1000000
```

Flashing goes through the SF32LB52's UART debug port (the CH343P's DTR/RTS
are not wired), needs sftool's compatibility mode on this board, and a
transfer occasionally times out; `flash.sh` retries up to four times. If
every attempt fails, LilyGo's advice is to re-plug the USB cable and retry;
the battery must be connected or the charger IC keeps resetting the board.

## ESP32 variant (`esp32/`)

PlatformIO project for the original ESP32 T-Display (`tdisplay`) and the
T-Display-S3 (`tdisplay_s3`). The IR LED needs an external driver on the
pin set in `esp32/src/board_config.h` (GPIO 27 / GPIO 2 by default). The
same serial commands are available at 115200 baud; the two on-board buttons
work as "next field" / "change value" (hold left = send, hold right = value
down).

Install PlatformIO Core so `pio` is available on your `PATH`. Its dependency
manager downloads the platform and libraries declared in
[`esp32/platformio.ini`](esp32/platformio.ini); this variant does not use
the SiFli SDK.

Build either board from the repository root:

```sh
pio run -d esp32 -e tdisplay
pio run -d esp32 -e tdisplay_s3
```

Upload to the connected board, selecting the matching environment and
replacing the example port:

```sh
pio run -d esp32 -e tdisplay -t upload --upload-port /dev/ttyUSB0
```

## Caveats

* The real ARC466A41 captures covered COOL and FAN with the fan speeds and
  all set-points. DRY mode (temperature byte 0xC0), swing, Powerful, Econo,
  Quiet, Sensor, Comfort and the timers use the bit positions documented for
  sibling remotes (ARC466A33 / ARC466A9 / ARC452A4) and the IRremoteESP8266
  library; they are consistent across sources but were not captured from an
  A41.
* The frame overwrites the indoor unit's clock on every transmission, so
  set the RTC if you rely on the unit's timers.
* The weekly-timer programme itself and the fault-diagnosis mode are
  separate frame types the emulator does not implement.

## License

Original project code is available under the [MIT License](LICENSE).
Exceptions for adapted third-party code are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). External SDKs, libraries
and reference material retain their own licenses.
