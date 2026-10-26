# Daikin ARC466A41 — IR protocol specification for firmware emulation

Research snapshot: 2026-10-12. `R/` denotes the original `research/` source
cache and `scratch/` the analysis scripts used for this investigation. Those
downloaded sources and working files are not distributed with this repository;
public source links are provided below. The maintained encoder and tests are in
[`protocol/`](../protocol/).
IRremoteESP8266 checkout: commit `1e2f0f3ef0a93cbf2a8ddb2e95130f8f4c584b3f`,
`R/IRremoteESP8266/library.properties:2 version=2.9.0`. All timings in microseconds (µs).

---

## 0. Verdict

**The ARC466A41 transmits the IRremoteESP8266 `DAIKIN` protocol**: 280 data bits = 35 bytes,
sent as a 5-zero-bit preamble plus three sections of 8 / 8 / 19 bytes, LSB-first per byte,
38 kHz carrier, one transmission per key press, no repeat frame.
**Confidence: high.**

Evidence chain:

1. **407 direct captures of an ARC466A41** (SmartIR PR #1021, https://github.com/smartHomeHub/SmartIR/pull/1021,
   file `codes/climate/1119.json`, saved as `R/r3-arc466a41/smartir_1119_arc466a41.json`; PR body:
   "Daikon ARC466A41 / Tested with a CTKM50VVMG unit"; `supportedModels` begins `"ARC466A41"`).
   Independent re-decode at the correct Broadlink tick (2^-15 s = 30.5176 µs):
   `scratch/a41_robust.mjs 30.5176` → `commands=407 canonical(584 entries, 6/5 preamble, 3 sections)=348
   repeatByteValues=0`; `scratch/verify_1119_independent.js` → `section shapes { '64/64/152': 407 }`, `chkFail 0`.
   The 59 non-canonical packets are Broadlink learner glitches (582 = first preamble pair missed,
   586/588 = one spurious extra pair); their data bytes decode identically.
2. **Sibling remotes documented as DAIKIN**: `R/IRremoteESP8266/src/ir_Daikin.h:48-49`
   `//   Brand: Daikin,  Model: ARC466A12 remote (DAIKIN)` / `ARC466A33 remote (DAIKIN)`;
   ARC466A33 capture in `R/github/issue_1101.json` (`Protocol  : DAIKIN`, 280 bits);
   ARC466A9 encoder `R/writeups/BokoEnos-daikin_ir_encoder` (3-frame transport, identity byte 5 = 0x10 like the A41).
3. **Official fitment**: Daikin AU manual 3P472684-2, `R/r3-arc466a41/daikin_au_multisplit_3P472684.txt:656-657`
   `HEAT PUMP model ARC466A40` / `COOLING ONLY model ARC466A41` (CTXM/CTKM 20-46 RVMA); `:845-848`
   cooling-only mode sequence `DRY COOL FAN`; `:873-874` `COOL operation ... 18.0 – 32.0°C`; `:1423` `0.5°C up in COOL`.

Why not the other Daikin protocols (each excluded by the A41 captures):

| Candidate | Wire signature (source) | A41 captures |
|---|---|---|
| DAIKIN312 (ARC466A58/A67) | same 5-zero-bit leader (`ir_Daikin.cpp sendDaikin312`), but **2 sections 20+19 bytes, 36.7 kHz** (`ir_Daikin.h:811 kDaikin312Freq = 36700`), 644 raw entries (`test/ir_Daikin_test.cpp:4022`) | 3 sections 8/8/19, 584 entries → excluded |
| DAIKIN2 (ARC477A1) | leader `kDaikin2LeaderMark = 10024; kDaikin2LeaderSpace = 25180` (`ir_Daikin.h:278-279`), 2 sections, 36.7 kHz | no 10 ms leader mark → excluded |
| DAIKIN216 (ARC433B69/ARC484A4) | no leader, 2 sections 8+19, byte 3 = 0xF0 | leader present, byte 3 = 0x00, 3 sections → excluded |
| DAIKIN152 (ARC480) | 1 section of 19 bytes, `kDaikin152OneSpace = 1529`, `kDaikin152Gap = 25182` (`ir_Daikin.h:622-629`) | 3 sections, one-space 1282 → excluded |
| ESPHome `daikin` component | no preamble, header 3360/1760, bits 520/1370/360, gap 32300 (`R/esphome/daikin.h:42-48`) | A41 sends a preamble + 25 ms gap, 3479/1740, 458/1282/427, 35.5 ms; explains "it sometimes work using the standard `daikin` platform, but it is no stable" (`R/r3-arc466a41/esphome_fr2360_comments.json`, lrodorigo 2024-07-20) |

Note the "ARC466" prefix alone is not a protocol indicator: ARC466A58/A67 are DAIKIN312
(`R/r3-arc466a41/gh_irremote_issue_1829_comments.json`: "322 Bits, ARC466A67, FTXM20R5V1B"). The A41 verdict rests on its own captures.

Important nuance: the A41's 19-byte state frame carries two constants (byte 31 = 0xC5, byte 33 = 0x08) that
IRremoteESP8266 documents for the newer DAIKIN312/DAIKIN152 state frame (`ir_Daikin.h:790-791` "Byte 35: Always 0xC5",
`:804` "Byte 37: Always 0x08"; `ir_Daikin.cpp:3104` `_.raw[15] = 0xC5;` for DAIKIN152) but which the DAIKIN union treats as
anonymous padding. They must be copied verbatim (see §5).

---

## 1. Carrier

| Parameter | Value | Provenance |
|---|---|---|
| Frequency | **38 kHz** (37.7-38.0 measured/accepted) | `R/IRremoteESP8266/src/ir_Daikin.cpp:78` passes `38` (kHz; `src/IRsend.cpp:106-107` `if (freq < 1000) ... freq *= 1000;`); `test/ir_Daikin_test.cpp:26 "f38000d50"`; `R/esphome/daikin.h:42 DAIKIN_IR_FREQUENCY = 38000;`; ToniA `DaikinHeatpumpIR.cpp:111 IR.setFrequency(38);`; Flipper `Daikin_FTXS25KVMA.ir:9 frequency: 38000`; JP1 learner on ARC433A1 `R/writeups/hifi-remote/p82812_archive.txt:90 freq: 37.7 kHz` |
| Duty cycle | **unmeasured on the real remote**; use 33-50 % | IRremoteESP8266 sends 50 % (`src/IRsend.h:44 const uint8_t kDutyDefault = 50;`, `ir_Daikin.cpp:79` last arg `50`); ESPHome docs `carrier_duty_percent: 50%` (`R/esphome/climate_ir.mdx:56`); Flipper `duty_cycle: 0.330000` is the Flipper raw-file default, not a measurement |

Demodulating receivers (TSOP-class) only care about the carrier frequency; duty affects LED current and range.
50 % matches the library's test vectors; 33 % halves average LED current.

---

## 2. Timing table (µs)

"A41" = median [p5-p95] over the 348 canonical ARC466A41 captures (`scratch/a41_robust.mjs 30.5176`).
"Recommended" is what the emulator should transmit. The cell periods (mark+space) are the invariant quantity across
nine independent captures from four receiver types; absolute mark width is receiver-dependent (390-565 observed),
so the recommended mark is the Broadlink/Flipper consensus.

| # | Element | A41 measured | Other real captures (3-section family) | IRremoteESP8266 `sendDaikin` | ESPHome `daikin` | **Recommended** |
|---|---|---|---|---|---|---|
| 1 | Preamble mark (×6) | 458 [397-488] | Flipper 457-484; issue #1101 444-476; RealExample 416; AnalysIR (TSOP) 560-592 | 428 | — (none) | **450** |
| 2 | Preamble short space (×5) | 397 [366-458] | Flipper 387-415; #1101 390-422; RealExample 446; AnalysIR 272-300 | 428 | — | **410** |
| 3 | Gap after preamble | **25147 [25116-25177]** | Flipper 25430/25407/25103/25101/25350; #1101 25086; RealExample 25434; AnalysIR 25180 | 29428 (= 428 + `kDaikinGap` 29000, `ir_Daikin.h:178,180`) | — | **25150** |
| 4 | Section header mark | **3479 [3418-3540]** | Flipper 3488-3545; #1101 3502-3532; RealExample 3436; AnalysIR 3572-3604; hifi-remote `+3440`; rdlab/YSYouJack 3400; harizanov `#3500` | 3650 (`ir_Daikin.h:175`; its comment `// kDaikinBitMark * 8` is wrong, 428×8 = 3424) | 3360 | **3480** |
| 5 | Section header space | **1740 [1678-1770]** | Flipper 1695-1727; #1101 1662-1694; RealExample 1768; AnalysIR 1588-1620; hifi `-1745`; rdlab 1750; harizanov `_1700` | 1623 (`ir_Daikin.h:176`; comment `* 4` wrong, 428×4 = 1712) | 1760 | **1740** |
| 6 | Bit mark | 458 [397-488] | Flipper 447-465; #1101 462-466; RealExample 390-416; AnalysIR 554-565 | 428 | 520 | **450** |
| 7 | '0' space | 427 [366-458] (mean 413) | Flipper 397-419; #1101 398-402; RealExample 446; AnalysIR 292-306 | 428 | 360 | **415** |
| 8 | '1' space | 1282 [1221-1312] (mean 1281) | Flipper 1277-1299; #1101 1262-1268; RealExample 1336; AnalysIR 1167-1176 | 1280 | 1370 | **1285** |
| 9 | '0' cell period (mark+space) | 868 | 856-866 | 856 | 880 | **865** |
| 10 | '1' cell period | 1735 | 1728-1747 | 1708 | 1890 (+9 %) | **1735** |
| 11 | Footer mark after each section | 458 [397-519] | Flipper 447-470; #1101 442-472; RealExample 390-416 | 428 | 520 | **450** |
| 12 | Gap after section 1 and after section 2 | **35553 [35492-35614]** | Flipper 34986-35504; #1101 35444; RealExample 34878/34876; AnalysIR 34696/34664 | 29428 | 32300 | **35550** |
| 13 | After section 3 | footer mark, then silence (Broadlink records its 101.7 ms capture timeout; Flipper files end on the mark) | — | `m428 s29428` | `mark(520); space(0)` | footer mark, then idle **≥ 30 ms** |
| 14 | Repeat | none (Broadlink repeat byte = 0 in all 407 packets; one transmission per packet) | one per Flipper capture | `kDaikinDefaultRepeat = kNoRepeat` (`src/IRremoteESP8266.h:1172,1222`) | single | **single transmission** |

Decoder tolerance: `ir_Daikin.h:173 const uint8_t kDaikinTolerance = 35;` — the library's own 29428 gap constant accepts
both real gaps (29428 × [0.65, 1.35] = 19128..39728), which is why IRrecvDumpV2 decodes real remotes cleanly even though
the library's transmit constants differ from them.

Broadlink tick caveat: the saved file `scratch/decoded_1119.txt:1` says `unit_us: 32.84`; that is the inverted formula
(python-broadlink `protocol.md`: "Pulse lengths in 2^-15 s units (µs * 269 / 8192 works very well)" → 1 tick = 30.52 µs).
Numbers decoded at 32.84 are 7.6 % too long (e.g. header 3744/1839, gap 38259) and must not be used. All A41 numbers in
this document use 30.5176 µs and agree with the non-Broadlink captures to within 1-2 %.

---

## 3. Transmit sequence (one key press)

Bytes are `b[0..34]`. "mark" = carrier on, "space" = carrier off. All values from the **Recommended** column above.

```
0. Carrier 38 kHz, duty 33-50 %.

1. PREAMBLE (five '0' bit cells + a terminating mark):
     repeat 5 times:  mark 450, space 410
     then:            mark 450
     then:            space 25150                      (gap after preamble)

2. SECTION 1 = b[0..7]  (8 bytes, 64 bits):
     mark 3480, space 1740                              (section header)
     for byte in b[0..7]:
        for bit = 0..7 (LSB first):  mark 450, space (bit ? 1285 : 415)
     mark 450                                           (footer mark, mandatory)
     space 35550                                        (section gap)

3. SECTION 2 = b[8..15] (8 bytes, 64 bits):
     mark 3480, space 1740
     64 bit cells as above
     mark 450
     space 35550

4. SECTION 3 = b[16..34] (19 bytes, 152 bits):
     mark 3480, space 1740
     152 bit cells as above
     mark 450                                           (footer mark)
     idle >= 30 ms before any further transmission       (end; no repeat)
```

Entry count per transmission (marks + spaces): preamble 12 (6 marks, 5 spaces, 1 gap) + section 1 (2 + 128 + 2)
+ section 2 (2 + 128 + 2) + section 3 (2 + 304 + 2) = **584** including the final silence, **583** when the recorder
stops at the last mark (IRremoteESP8266 `rawData[583]`, Flipper 583 values, AnalysIR `Raw: (583)`). Bit cells on
the wire: 5 (preamble) + 64 + 64 + 152 = 285; data bits = 280. On-air time ≈ 420-450 ms depending on bit content
(BokoEnos README: "584 edges, ~450 ms on air").

Equivalent library form (`R/IRremoteESP8266/src/ir_Daikin.cpp:67-106`): `sendGeneric(0, 0, kDaikinBitMark, kDaikinOneSpace,
kDaikinBitMark, kDaikinZeroSpace, kDaikinBitMark, kDaikinZeroSpace + kDaikinGap, static_cast<uint64_t>(0b00000),
kDaikinHeaderLength, 38, false, 0, 50);` followed by three `sendGeneric(kDaikinHdrMark, kDaikinHdrSpace, ...)` calls over
`kDaikinSection1Length` = 8, `kDaikinSection2Length` = 8 and the remaining 19 bytes (`ir_Daikin.h:155-158`). Its expected
output string, `test/ir_Daikin_test.cpp:26-28`: `"f38000d50" "m428s428m428s428m428s428m428s428m428s428" "m428s29428" "m3650s1623" ...`.

Why the preamble and footer marks matter: every genuine capture has them (348/348 canonical A41 packets: 6 marks/5 spaces;
Flipper `Daikin_FTXS25KVMA.ir:11` `484 388 457 415 458 389 484 387 458 389 484 25430 3545 1717 ...`; issue #1101
`448, 390, 472, 422, 444, 392, 476, 418, 444, 392, 474, 25086,`). BokoEnos `_encoder.py:46-49`: "5 short pulse pairs
followed by a trailing mark — confirmed by direct capture inspection. Without the trailing mark the AC ignores the frame."
and `:210-213`: the per-section stop mark is required "otherwise the receiver sees one merged off-period and rejects the frame."
ESPHome/ToniA/danny omit the preamble entirely, which is the most plausible cause of the ARC466 "not stable" report.

---

## 4. Bit order and polarity

- **LSB-first within each byte**, bytes in index order. `src/IRsend.cpp:288-291`
  `for (uint16_t bit = 0; bit < nbits; bit++, data >>= 1) if (data & 1) { mark(onemark); space(onespace);` with
  `MSBfirst = false` at `ir_Daikin.cpp:78,86,91,98,104`; ESPHome `daikin.cpp:46 for (uint8_t mask = 1; mask > 0; mask <<= 1)`;
  BokoEnos `_encoder.py:35 (byte >> b) & 1 for byte in data for b in range(8)`.
- **Long space (≈1.28-1.34 ms) = '1'; short space (≈0.40-0.45 ms) = '0'.** Capture check: Flipper FTXS25KVMA after the
  header `430 1314 431 415 457 415 457 415 486 1258` = bits 1,0,0,0,1 = 0x11 LSB-first. (rdlab `page.txt:660-662`
  states the opposite and is wrong.)
- Throughout the tables below, **bit 0 = least-significant bit = first bit transmitted**.

---

## 5. The 35-byte frame

Union source: `R/IRremoteESP8266/src/ir_Daikin.h:73-138` (`union DaikinESPProtocol`). Constants `ir_Daikin.h:141-164`.
Setters `ir_Daikin.cpp:215-470`. "A41" column = value seen in the ARC466A41 captures (`scratch/decoded_1119.txt`,
`scratch/verify_1119_independent.js`).

### Section 1 — identity/comfort frame, bytes 0-7. Sum1 at byte 7.

| Byte | Bits | Field | Values / notes | A41 |
|---|---|---|---|---|
| 0 | 7..0 | fixed | `0x11` | 0x11 |
| 1 | 7..0 | fixed | `0xDA` | 0xDA |
| 2 | 7..0 | fixed | `0x27` | 0x27 |
| 3 | 7..0 | fixed | `0x00` | 0x00 |
| 4 | 7..0 | frame type | `0xC5` (`ir_Daikin.cpp:167`; ESPHome `DAIKIN_FRAME_TYPE_COMFORT = 0xC5`) | 0xC5 |
| 5 | 7..0 | opaque / identity | library default `0x00`; ARC466A41 `0x10`; ARC466A9 `0x10` (BokoEnos: "0x20 correlated with battery state"); ARC466A33 `0x30`; ARC452A4 `0x00`. **Copy from capture.** | **0x10** |
| 6 | 4 | **Comfort** airflow | 1 = on (`ir_Daikin.cpp:377-379`; ESPHome `0x10`). Other bits 0. | 0x00 |
| 7 | 7..0 | **Sum1** | `(b0+b1+b2+b3+b4+b5+b6) & 0xFF`. A41: 0x11+0xDA+0x27+0x00+0xC5+0x10+0x00 = 0x1E7 → `0xE7`; comfort on → `0xF7` | 0xE7 |

### Section 2 — clock frame, bytes 8-15. Sum2 at byte 15.

| Byte | Bits | Field | Values / notes | A41 |
|---|---|---|---|---|
| 8-10 | | fixed | `0x11 0xDA 0x27` | |
| 11 | 7..0 | fixed | `0x00` | 0x00 |
| 12 | 7..0 | frame type | `0x42` (`ir_Daikin.cpp:172`) | 0x42 |
| 13 | 7..0 | **CurrentTime[7:0]** | minutes past midnight, low byte | 0x37..0x53 |
| 14 | 2..0 | **CurrentTime[10:8]** | 11-bit field (`ir_Daikin.h:88`), range 0..1439 (library guard `ir_Daikin.cpp:441 if (mins > 24 * 60) mins = 0;` is off by one — clamp to 1439 yourself) | |
| 14 | 5..3 | **CurrentDay** | 0 = unset ("UNKNOWN"), 1 = Sun, 2 = Mon, 3 = Tue, 4 = Wed, 5 = Thu, 6 = Fri, 7 = Sat (`ir_Daikin.h:89`, `.cpp:454-456`) | 0x33 (day 6 Fri, min bits 8-10 = 3) |
| 14 | 7..6 | unused | 0 | |
| 15 | 7..0 | **Sum2** | `(b8+...+b14) & 0xFF`; with time 0 / day 0 = `0x54` | |

Encode: `b13 = t & 0xFF; b14 = ((t >> 8) & 0x07) | (day << 3)`.
Examples (all verified): A41 `3A 33` → t = 0x33A = 826 = 13:46, day 6 (Fri); RealExample `3A 05` → 1338 = 22:18, day 0;
ARC452A4 `E3 0B` → 995 = 16:35, day 1 (Sun); ESPHome's hard-coded `49 05` → 1353 = 22:33, day 0 (never updated).
The manual (`daikin_au_multisplit_3P472684.txt`) notes the ON/OFF and WEEKLY timers rely on the indoor unit's clock, which this
frame sets on every transmission — regenerate bytes 13-14 (and Sum2) from a real clock if timers are used; any fixed value is
otherwise accepted (the CTKM50VVMG accepted all 407 frames).

### Section 3 — state frame, bytes 16-34. Sum3 at byte 34.

| Byte | Bits | Field | Values / notes | A41 |
|---|---|---|---|---|
| 16-18 | | fixed | `0x11 0xDA 0x27` | |
| 19 | 7..0 | fixed | `0x00` | 0x00 |
| 20 | 7..0 | frame type | `0x00` (ESPHome `DAIKIN_FRAME_TYPE_STATE = 0x00`) | 0x00 |
| 21 | 0 | **Power** | 1 = on (`ir_Daikin.cpp:215-217`) | |
| 21 | 1 | **OnTimer** enable | 1 = on-timer armed (`.cpp:389-392`) | 0 |
| 21 | 2 | **OffTimer** enable | 1 = off-timer armed (`.cpp:414-417`) | 0 |
| 21 | 3 | **always 1** | `ir_Daikin.h:100 uint64_t :1;  // always 1`; set via `stateReset` `_.raw[21] = 0x49`. BokoEnos README calls it "Variant ID: 1 for ARC466A9 (3-frame), 0 for ARC480A11 (1-frame)". ESPHome never sets it (`daikin.cpp:82-107`) — do set it. | 1 |
| 21 | 6..4 | **Mode** | `kDaikinAuto = 0b000`, `kDaikinDry = 0b010`, `kDaikinCool = 0b011`, `kDaikinHeat = 0b100`, `kDaikinFan = 0b110` (`ir_Daikin.h:141-145`); other values → Auto (`.cpp:268-280`). A41 is cooling-only: DRY/COOL/FAN only (manual `:845-848`). | 3 (cool), 6 (fan) |
| 21 | 7 | unused | 0 | 0 |
| | | whole-byte examples | on+cool `0x39`; on+fan `0x69`; off (keeps last mode nibble) `0x68`; on+dry `0x29`; on+auto `0x09`; on+heat `0x49`; on+cool+both timers armed `0x3F` | 0x39 / 0x69 / 0x68 |
| 22 | 7..0 | **Temp** | `round(°C × 2)`, odd = half degree. Library clamps 10..32 °C → 20..64 (`ir_Daikin.h:146-147`, `.cpp:227-231` — note it truncates the float; round to nearest 0.5 °C yourself). A41 COOL range 18.0-32.0 °C → `0x24..0x40`, half degrees confirmed (18.5 → `0x25`, 22.5 → `0x2D`). **FAN mode sends `0x32` (25 °C)** (A41 confirmed). **DRY mode sends `0xC0`** (`ir_Daikin.h:142 // temp 0xc0 = 96 degrees c`; ESPHome and BokoEnos agree; not captured on the A41). | 0x24..0x40, 0x32 |
| 23 | 7..0 | fixed | `0x00` | 0x00 |
| 24 | 3..0 | **SwingV** (flap, up/down) | `0x0` off, `0xF` on (`kDaikinSwingOff/On`, `.cpp:284-286`) | 0x0 |
| 24 | 7..4 | **Fan** | speeds 1..5 → nibble `0x3..0x7` (`fanset = 2 + fan`, `.cpp:240-250`); `0xA` = Auto (`kDaikinFanAuto`); `0xB` = indoor-unit Quiet/"night" (`kDaikinFanQuiet`); invalid → Auto. Whole-byte values seen on the A41: `0x30 0x40 0x50 0x60 0x70 0xA0 0xB0`. | all seven |
| 25 | 3..0 | **SwingH** (louvre, left/right) | `0x0` off, `0xF` on (`.cpp:296-298`). "3-D airflow" = SwingV + SwingH both on. | 0x0 |
| 25 | 7..4 | unused | 0 | 0 |
| 26 | 7..0 | **OnTime[7:0]** | see timer encoding below | 0x00 |
| 27 | 3..0 | **OnTime[11:8]** | | 0x6 |
| 27 | 7..4 | **OffTime[3:0]** | | 0x0 |
| 28 | 7..0 | **OffTime[11:4]** | | 0x60 |
| 29 | 0 | **Powerful** | 1 = on (`.cpp:322-329`; ESPHome `0x01`) | 0 |
| 29 | 5 | **Quiet** (outdoor unit) | 1 = on (`.cpp:308-312`; ESPHome `0x20`) | 0 |
| 29 | 1-4, 6-7 | unused | 0 | |
| 30 | 7..0 | padding | `0x00` | 0x00 |
| 31 | 7..0 | opaque constant | library default `0xC0` (`.cpp:182`); **ARC466A41 `0xC5`** (= DAIKIN312/152 "Always 0xC5"); ARC466A33 and ARC452A4 `0xC1`. **Copy from capture.** | **0xC5** |
| 32 | 1 | **Sensor** (Intelligent Eye) | 1 = on (`.cpp:339-341`; ESPHome `0x02`) | 0 |
| 32 | 2 | **Econo** | 1 = on (`.cpp:351-355`; ESPHome `0x04`) | 0 |
| 32 | 7 | **WeeklyTimer disabled** | **1 = weekly timer OFF, 0 = weekly timer active**: `ir_Daikin.cpp:467-470 // Bit is cleared for on. _.WeeklyTimer = !on;`; BokoEnos README `| 16 | 7 | Weekly-timer disable | 1 = weekly schedule inactive, 0 = weekly active`; ARC452A4 vectors `0x80` = "without weekly timer", `0x00` = "with" (`test:573-580`). | 0x00 |
| 32 | 0, 3-6 | unused | 0 (ARC466A33 shows bit 4 set, meaning unknown) | |
| 33 | 1 | **Mold** (mould-proof) | 1 = on (`.cpp:365-367`) | 0 |
| 33 | 0, 2-7 | opaque | **ARC466A41 always `0x08`** (bit 3; = DAIKIN312 "Byte 37: Always 0x08"). Meaning unknown. **Copy from capture.** | **0x08** |
| 34 | 7..0 | **Sum3** | `(b16+...+b33) & 0xFF` | |

### Timer encoding (bytes 26-28): two 12-bit fields packed LSB-first

```
b26 = on & 0xFF
b27 = ((on >> 8) & 0x0F) | ((off & 0x0F) << 4)
b28 = (off >> 4) & 0xFF
decode:  on  = b26 | ((b27 & 0x0F) << 8)
         off = (b27 >> 4) | (b28 << 4)
```
- Declared as `uint64_t OnTime :12; uint64_t OffTime :12;` (`ir_Daikin.h:115-116`, comment "timer mins past midnight").
  Bit 11 of each field is always 0 in practice (max meaningful value 0x600).
- **Disabled** = `kDaikinUnusedTime = 0x600` (1536, "25:36", `ir_Daikin.h:164`), written by `disableOnTimer()/disableOffTimer()`
  (`.cpp:395-398, 420-423`). Both disabled → bytes 26-28 = `00 06 60` (every A41 capture).
- **Enabled**: set byte 21 bit 1 (on) and/or bit 2 (off) and write minutes-past-midnight. RealExample `0A 25 17` with
  byte 21 = `0x3F`: on = 0x50A = 1290 = 21:30, off = 0x172 = 370 = 06:10 (`test:868-873`).
- Semantics: time-of-day on clocked remotes such as the ARC466A41 (has CLOCK and day-of-week; BokoEnos: "interprets them as
  time-of-day on devices shipped with ARC466A9 ... sentinel 0x600"); the same bits are a countdown on the clockless
  ARC480A11/ARC470A1. The A41 "NIGHT SET" button is an OFF TIMER whose 0.5 °C set-point drift is done by the indoor unit
  (manual `:1423`), so it needs no extra bits.

### Library-enforced exclusivity (setter behaviour, not wire constraints)

`setQuiet(true)` clears Powerful; `setPowerful(true)` clears Quiet and Econo; `setEcono(true)` clears Powerful
(`ir_Daikin.cpp:308-355`). Econo and Quiet may both be set ("ECONO + QUIET combination" on the remote).

---

## 6. Checksum

Each section ends with the sum of its preceding bytes, reduced modulo 256:

| Checksum byte | Bytes included in the sum |
|---|---|
| 7 | 0 through 6 |
| 15 | 8 through 14 |
| 34 | 16 through 33 |

Recompute all three sums after changing any settings. The same rule appears in
IRremoteESP8266's [byte-sum helper](https://github.com/crankyoldgit/IRremoteESP8266/blob/1e2f0f3ef0a93cbf2a8ddb2e95130f8f4c584b3f/src/IRutils.cpp#L451-L457)
and [Daikin checksum routine](https://github.com/crankyoldgit/IRremoteESP8266/blob/1e2f0f3ef0a93cbf2a8ddb2e95130f8f4c584b3f/src/ir_Daikin.cpp#L132-L158).

Verified on 9/9 library test vectors plus all 407 A41 captures (`scratch/daikin_layout_verify.mjs`, `scratch/verify_1119_independent.js chkFail 0`).
The library's `SendDataOnly` vector (`test:17-20`) has deliberately invalid Sum2/Sum3 and is a timing-format reference only.

---

## 7. Default state arrays

### 7a. Recommended firmware default for the ARC466A41 (power on, COOL, 25.0 °C, fan Auto, no swing, timers off, clock 00:00 day unset)

Derived from the captured `cool/auto/off/25` packet with the clock zeroed and all three sums recomputed:
```c
const uint8_t kA41Default[35] = {
  0x11, 0xDA, 0x27, 0x00, 0xC5, 0x10, 0x00, 0xE7,                 // section 1 (Sum1 = 0xE7)
  0x11, 0xDA, 0x27, 0x00, 0x42, 0x00, 0x00, 0x54,                 // section 2 (clock 00:00, day 0, Sum2 = 0x54)
  0x11, 0xDA, 0x27, 0x00, 0x00, 0x39, 0x32, 0x00, 0xA0, 0x00,     // section 3
  0x00, 0x06, 0x60, 0x00, 0x00, 0xC5, 0x00, 0x08, 0x50 };         //   (Sum3 = 0x50)
```
Only bytes 6, 13-14, 21, 22, 24, 25, 26-28, 29, 32 and 33 bit 1 should ever be modified; bytes 5, 31 and 33 bit 3 are
copied constants. Then recompute b7, b15, b34.

### 7b. IRremoteESP8266 `stateReset()` default (`ir_Daikin.cpp:161-185`; equals `test:638-642 longState`)

```c
0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
0x11, 0xDA, 0x27, 0x00, 0x42, 0x00, 0x00, 0x54, 0x11,
0xDA, 0x27, 0x00, 0x00, 0x49, 0x1E, 0x00, 0xB0, 0x00,
0x00, 0x06, 0x60, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x4F
```
= Power on, Heat, 15 °C, Fan Quiet, swings off, timers disabled, clock 00:00 day 0, weekly-timer bit 0 (`test:693-699`).
Not A41-like (byte 5 = 0x00, byte 31 = 0xC0, byte 33 = 0x00). If the IRDaikinESP class is used, load the A41 template with
`setRaw(kA41Default, 35)` (`.cpp:197-205` copies all 35 bytes; setters never touch the anonymous padding bytes) and never
call `stateReset()`.

---

## 8. Test vectors

All byte strings are section 1 | section 2 | section 3; every one re-verified for all three sums.

### V1 — ARC466A41, COOL, fan Auto, 25.0 °C, swing off (SmartIR 1119 `cool/auto/off/25`) — with raw timing array

```
11 DA 27 00 C5 10 00 E7 | 11 DA 27 00 42 39 33 C0 | 11 DA 27 00 00 39 32 00 A0 00 00 06 60 00 00 C5 00 08 50
```
Meaning: byte 21 `0x39` = power on, bit3, mode 3 COOL; byte 22 `0x32` = 25.0 °C; byte 24 `0xA0` = fan Auto, SwingV off;
byte 25 `0x00` SwingH off; timers `00 06 60` disabled; clock `39 33` = 0x339 = 825 = 13:45, day 6 (Fri); byte 32 `0x00`
weekly active; powerful/quiet/econo/sensor/comfort/mold all 0.
Raw capture (Broadlink, decoded at 30.5176 µs/tick by `scratch/dump_a41_vector.mjs cool/auto/off/25`; 584 entries,
mark/space alternating starting with a mark; the final 101715 is the learner's timeout, not remote behaviour):
```c
uint16_t raw_cool_auto_off_25[584] = {
  397, 397, 458, 427, 397, 458, 458, 427, 427, 427, 458, 25146,
  3448, 1740, 458, 1282, 458, 427, 427, 427, 458, 427, 427, 1282,
  458, 427, 458, 397, 458, 427, 427, 427, 458, 1282, 458, 427,
  427, 1282, 458, 1282, 458, 427, 427, 1312, 427, 1282, 458, 1282,
  458, 1282, 458, 1282, 458, 397, 458, 427, 458, 1282, 458, 397,
  458, 427, 427, 427, 458, 397, 458, 427, 458, 397, 458, 427,
  458, 397, 458, 427, 427, 427, 458, 1282, 458, 397, 458, 1282,
  458, 427, 427, 427, 458, 427, 427, 1282, 458, 1282, 458, 427,
  458, 397, 458, 427, 427, 427, 458, 1282, 458, 397, 458, 427,
  458, 397, 458, 427, 427, 427, 458, 427, 427, 427, 458, 397,
  458, 427, 458, 397, 458, 427, 427, 1282, 458, 1282, 458, 1282,
  458, 427, 458, 397, 458, 1282, 458, 1282, 458, 1282, 458, 35553,
  3479, 1709, 458, 1282, 458, 427, 427, 427, 458, 427, 427, 1282,
  458, 427, 458, 397, 458, 427, 458, 397, 458, 1282, 458, 397,
  458, 1282, 458, 1282, 458, 427, 427, 1282, 458, 1282, 458, 1282,
  458, 1282, 458, 1282, 458, 397, 458, 427, 458, 1282, 427, 427,
  458, 427, 427, 427, 458, 397, 458, 427, 458, 397, 458, 427,
  458, 397, 458, 427, 427, 427, 458, 397, 458, 1282, 458, 427,
  458, 397, 458, 427, 427, 427, 458, 1282, 458, 397, 458, 1282,
  458, 427, 458, 397, 458, 1282, 458, 1282, 458, 1282, 458, 397,
  458, 427, 427, 1282, 458, 1282, 458, 427, 458, 397, 458, 1282,
  458, 1282, 458, 397, 458, 427, 458, 397, 458, 427, 458, 397,
  458, 427, 427, 427, 458, 397, 458, 1282, 458, 1282, 458, 35553,
  3479, 1740, 458, 1282, 427, 427, 458, 427, 427, 427, 458, 1282,
  458, 397, 458, 427, 458, 397, 458, 427, 427, 1282, 458, 427,
  458, 1282, 458, 1282, 427, 427, 458, 1282, 458, 1282, 458, 1282,
  427, 1282, 458, 1282, 488, 397, 458, 397, 458, 1282, 458, 427,
  427, 427, 458, 397, 458, 427, 458, 397, 458, 427, 458, 397,
  458, 427, 427, 427, 458, 397, 458, 427, 488, 366, 458, 427,
  458, 397, 458, 427, 427, 427, 488, 397, 427, 427, 458, 1282,
  458, 397, 458, 427, 427, 1312, 427, 1282, 458, 1282, 458, 427,
  427, 427, 458, 427, 427, 1282, 458, 427, 458, 397, 458, 1282,
  458, 1282, 458, 397, 458, 427, 458, 397, 458, 427, 458, 397,
  458, 427, 427, 427, 458, 427, 427, 427, 458, 397, 458, 427,
  458, 397, 458, 427, 458, 397, 458, 427, 427, 1282, 458, 427,
  458, 1282, 458, 397, 458, 427, 427, 427, 458, 397, 458, 427,
  458, 397, 458, 427, 458, 397, 458, 427, 427, 427, 458, 427,
  427, 427, 458, 397, 458, 427, 458, 397, 458, 427, 427, 427,
  458, 1282, 458, 1282, 458, 397, 458, 427, 458, 397, 458, 427,
  427, 427, 458, 397, 458, 427, 458, 397, 458, 427, 458, 397,
  458, 1282, 458, 1282, 458, 397, 458, 427, 458, 397, 458, 427,
  427, 427, 458, 427, 427, 427, 458, 397, 458, 427, 458, 397,
  458, 427, 458, 397, 458, 427, 427, 427, 458, 397, 458, 427,
  458, 397, 458, 1282, 458, 427, 488, 1221, 458, 427, 458, 397,
  458, 427, 458, 1282, 427, 1282, 458, 427, 458, 397, 458, 427,
  458, 397, 458, 427, 427, 427, 458, 427, 427, 427, 458, 397,
  458, 427, 458, 397, 458, 1282, 458, 427, 427, 427, 458, 427,
  427, 427, 458, 397, 458, 427, 458, 397, 458, 427, 427, 1312,
  427, 427, 458, 1282, 458, 397, 458, 101715
};
```
Structure check on this array: entries [0..10] preamble (6 marks, 5 spaces), [11] = 25146 gap, [12..13] = 3448/1740 header,
[14..141] 64 bit cells, [142] footer mark, [143] = 35553, [144..145] header, [146..273] 64 cells, [274] footer, [275] = 35553,
[276..277] header, [278..581] 152 cells, [582] footer, [583] end.

### V2 — ARC466A41, power OFF (SmartIR 1119 `off`; remote last in COOL with fan Quiet)

```
11 DA 27 00 C5 10 00 E7 | 11 DA 27 00 42 52 33 D9 | 11 DA 27 00 00 68 32 00 B0 00 00 06 60 00 00 C5 00 08 8F
```
Meaning: byte 21 `0x68` = power **off**, bit3 set, mode nibble 6 (FAN) retained; byte 22 `0x32` (25 °C sent in FAN/OFF);
byte 24 `0xB0` = fan Quiet; clock `52 33` = 0x352 = 850 = 14:10 Fri. Note OFF keeps the mode/fan/temp bytes; ESPHome's
`0x00` for OFF is not what the remote sends.

### V3 — ARC466A41, FAN mode, fan Auto (SmartIR 1119 `fan_only/auto/off/25`)

```
11 DA 27 00 C5 10 00 E7 | 11 DA 27 00 42 53 33 DA | 11 DA 27 00 00 69 32 00 A0 00 00 06 60 00 00 C5 00 08 80
```
Meaning: byte 21 `0x69` = on + bit3 + mode 6 FAN; byte 22 `0x32` fixed 25 °C in FAN mode; byte 24 `0xA0` fan Auto.

### V4 — ARC466A41, COOL, fan Quiet, 18.5 °C (SmartIR 1119 `cool/quiet/off/18.5`) — half-degree check

```
11 DA 27 00 C5 10 00 E7 | 11 DA 27 00 42 37 33 BE | 11 DA 27 00 00 39 25 00 B0 00 00 06 60 00 00 C5 00 08 53
```
Meaning: byte 22 `0x25` = 37 = 18.5 °C; byte 24 `0xB0` = indoor Quiet fan; clock 0x337 = 823 = 13:43 Fri.
Further A41 single-byte checks: 18.0 °C `0x24`, 32.0 °C `0x40`, fan levels 1-5 `0x30/0x40/0x50/0x60/0x70` (`scratch/decoded_1119.txt`).

### V5 — IRremoteESP8266 `RealExample` (@sillyfrog capture, `test/ir_Daikin_test.cpp:798-874`) — with timers armed

```
11 DA 27 00 C5 00 00 D7 | 11 DA 27 00 42 3A 05 93 | 11 DA 27 00 00 3F 3A 00 A0 00 0A 25 17 01 00 C0 00 00 32
```
Library decode (`test:866-873`): "Power: On, Mode: 3 (Cool), Temp: 29C, Fan: 10 (Auto), Powerful: On, Quiet: Off, Sensor: Off,
Mould: Off, Comfort: Off, Swing(H): Off, Swing(V): Off, Clock: 22:18, Day: 0 (UNKNOWN), On Timer: 21:30, Off Timer: 06:10,
Weekly Timer: On". Byte 21 `0x3F` = power + onTimer + offTimer + bit3 + COOL; byte 29 `0x01` Powerful. Different remote
(byte 5 = 0x00, byte 31 = 0xC0) but the same protocol. The original 583-duration raw capture ends at the footer mark and is available in
[IRremoteESP8266's RealExample test](https://github.com/crankyoldgit/IRremoteESP8266/blob/1e2f0f3ef0a93cbf2a8ddb2e95130f8f4c584b3f/test/ir_Daikin_test.cpp#L798-L874),
credited there to @sillyfrog. The decoded state bytes above retain the useful
comparison with the ARC466A41 capture.

### V6 — ARC466A33 (issue #1101, `R/github/issue_1101.json`), swing on both axes, weekly timer disabled

Code `0x11DA2700C530000711DA27004207086311DA2700003932006F0F0006600000C19000B2` (280 bits) =
```
11 DA 27 00 C5 30 00 07 | 11 DA 27 00 42 07 08 63 | 11 DA 27 00 00 39 32 00 6F 0F 00 06 60 00 00 C1 90 00 B2
```
Library decode: "Power: On, Mode: 3 (Cool), Temp: 25C, Fan: 4 (UNKNOWN), ... Swing(H): On, Swing(V): On, Clock: 00:07, Day: 1 (Sun)".
Byte 24 `0x6F` = fan nibble 6 (speed 4) + SwingV on; byte 25 `0x0F` SwingH on; byte 32 `0x90` = weekly disabled (bit 7) + unknown bit 4.
Reporter timings: `kHdrMark = 3440 kHdrSpace = 1720 kBitMark = 430 kOneSpace = 1320 kZeroSpace = 430 kSpaceGap1 = 35500 kSpaceGap2 = 25000`;
auto-analysed: `3522 / 1673 / 463 / 1266 / 400 / 35444 / 25086`. The full `rawData[583]` is in the issue body (JSON key `body`).

### V7 — ARC452A4 weekly-timer pair (issue #704, `test/ir_Daikin_test.cpp:573-580`), power off

```
weekly ON : 11 DA 27 00 C5 00 00 D7 | 11 DA 27 00 42 E3 0B 42 | 11 DA 27 00 00 68 32 00 30 00 00 06 60 00 00 C1 00 00 03
weekly OFF: 11 DA 27 00 C5 00 00 D7 | 11 DA 27 00 42 E3 0B 42 | 11 DA 27 00 00 68 32 00 30 00 00 06 60 00 00 C1 80 00 83
```
Only byte 32 bit 7 and Sum3 differ. Clock `E3 0B` = 995 = 16:35, day 1 (Sun). Byte 24 `0x30` = fan speed 1.

---

## 9. Reference encoder (pseudo-C for an RMT/raw-timing transmitter)

```c
#define CARRIER_HZ   38000   // duty 33-50 %
#define PRE_MARK       450
#define PRE_SPACE      410
#define PRE_GAP      25150
#define HDR_MARK      3480
#define HDR_SPACE     1740
#define BIT_MARK       450
#define ZERO_SPACE     415
#define ONE_SPACE     1285
#define FOOT_MARK      450
#define SECTION_GAP  35550
#define END_GAP      30000   // or just stop; any idle >= 30 ms

static const uint8_t kSectionLen[3] = {8, 8, 19};

void daikin_fix_checksums(uint8_t b[35]) {
  uint8_t s = 0; for (int i = 0;  i < 7;  i++) s += b[i];  b[7]  = s;
  s = 0;         for (int i = 8;  i < 15; i++) s += b[i];  b[15] = s;
  s = 0;         for (int i = 16; i < 34; i++) s += b[i];  b[34] = s;
}

// emit(mark_us, space_us) appends one carrier burst followed by one idle period.
void daikin_send(const uint8_t b[35]) {
  for (int i = 0; i < 5; i++) emit(PRE_MARK, PRE_SPACE);
  emit(PRE_MARK, PRE_GAP);
  int idx = 0;
  for (int s = 0; s < 3; s++) {
    emit(HDR_MARK, HDR_SPACE);
    for (int n = 0; n < kSectionLen[s]; n++, idx++)
      for (int bit = 0; bit < 8; bit++)
        emit(BIT_MARK, (b[idx] >> bit) & 1 ? ONE_SPACE : ZERO_SPACE);   // LSB first
    emit(FOOT_MARK, s < 2 ? SECTION_GAP : END_GAP);
  }
}
```
Field helpers (apply to a copy of `kA41Default`, then `daikin_fix_checksums`):
```c
b[21] = (b[21] & 0x8E) | (power ? 1 : 0) | (onTimer ? 2 : 0) | (offTimer ? 4 : 0) | 0x08 | ((mode & 7) << 4);
b[22] = (mode == DRY) ? 0xC0 : (mode == FAN) ? 0x32 : (uint8_t)lroundf(tempC * 2.0f);   // tempC clamped 18.0..32.0 (COOL on A41)
b[24] = (fanNibble << 4) | (swingV ? 0x0F : 0x00);  // fanNibble: 3..7 = speed 1..5, 0xA auto, 0xB quiet
b[25] = swingH ? 0x0F : 0x00;
b[26] = on & 0xFF; b[27] = ((on >> 8) & 0x0F) | ((off & 0x0F) << 4); b[28] = (off >> 4) & 0xFF;  // 0x600 = disabled
b[29] = (powerful ? 0x01 : 0) | (quietOutdoor ? 0x20 : 0);
b[32] = (sensor ? 0x02 : 0) | (econo ? 0x04 : 0) | (weeklyTimerActive ? 0x00 : 0x80);
b[33] = 0x08 | (mold ? 0x02 : 0);
b[6]  = comfort ? 0x10 : 0x00;
b[13] = clockMin & 0xFF; b[14] = ((clockMin >> 8) & 0x07) | (day << 3);   // clockMin 0..1439, day 0..7
```
This produces exactly the bytes of V1-V4 for the corresponding settings (verified against `scratch/decoded_1119.txt`).

Using IRremoteESP8266 instead: `IRDaikinESP ac(pin); ac.begin(); ac.setRaw(kA41Default, 35); ac.setTemp(22.5); ... ac.send();`
transmits with the library constants (3650/1623, 428/428/1280, 29428 gaps) — within the 35 % decoder tolerance but not
what the real remote emits; acceptance by the indoor unit with those constants is untested here. Note the library produces the
carrier in software (`src/IRsend.cpp:164-207 IRsend::mark` busy-waits; `src/IRsend.h:33-35 kPeriodOffset = -2` on ESP32),
so a hardware-timed path (ESP-IDF `rmt` with carrier, or ESPHome `remote_transmitter`) with the recommended constants is preferred.

---

## 10. Receiving (if the firmware also decodes the real remote)

`R/IRremoteESP8266/src/ir_Daikin.cpp:602-662 decodeDaikin`: leader matched with `matchData(..., kDaikinHeaderLength, kDaikinBitMark,
kDaikinOneSpace, kDaikinBitMark, kDaikinZeroSpace, kDaikinTolerance, kDaikinMarkExcess, false)` and
`if (data_result.data) return false;  // The header bits should be zero.`; then one footer `matchMark(kDaikinBitMark)` +
`matchSpace(kDaikinZeroSpace + kDaikinGap)`; then three `matchGeneric` sections with footer `kDaikinBitMark, kDaikinZeroSpace + kDaikinGap`
(`atleast` on the last). Strict mode also requires 280 bits and `validChecksum`. A Broadlink/Flipper-style raw capture of the
remote decodes as `DAIKIN` with `IRrecvDumpV2` (confirmed for ARC466A33 in issue #1101 and for all 407 A41 packets offline).

---

## 11. Caveats, limits and what the ARC466A41 can do that this frame cannot encode

Encodable (bit positions verified against the union and the A33/A9/A41 captures):
ON/OFF; MODE DRY/COOL/FAN (AUTO/HEAT bits exist but the cooling-only CTKM units do not offer them); TEMP 18.0-32.0 °C in 0.5 °C
steps; FAN auto / indoor QUIET / 1-5; SWING flap (V) and louvre (H), "3-D airflow" = both; POWERFUL; OUTDOOR UNIT QUIET; ECONO
(and ECONO+QUIET together); SENSOR (INTELLIGENT EYE); COMFORT AIRFLOW; MOULD-PROOF; ON TIMER / OFF TIMER (NIGHT SET) / TIMER CANCEL;
CLOCK + day-of-week; weekly-timer active/inactive flag.

NOT encodable in the 280-bit frame:
- **WEEKLY TIMER programme content** (WEEKLY / PROGRAMME / COPY / BACK / NEXT, manual `:685-691`): the schedule is uploaded by a
  separate frame type not modelled in any source (BokoEnos README: "the schedule itself is programmed via a separate command not
  handled by this library"). Only the enable/disable flag (byte 32 bit 7) is covered.
- **Fault-diagnosis mode** (manual `:2476-2483`, hold CANCEL ~5 s): uses a diagnostic frame type (mildsunrise `DIAGNOSTIC = 0x01`), not modelled.
- Remote-local behaviour with no IR content: display backlight, key beep, the "NIGHT SET" 0.5 °C drift (done by the unit).
- HEAT / AUTO / HEAT PLUS: encodable, but absent from the ARC466A41 (cooling-only; ARC466A40 only) and ignored by CTKM units.

Caveats:
- Bytes 5 (0x10), 31 (0xC5) and 33 bit 3 (0x08) have unknown meaning; copy verbatim. Sending the library defaults (0x00/0xC0/0x00)
  is a different remote's identity and is untested on A41 units.
- The A41 captures exercise only: COOL and FAN modes, all seven fan codes, 18-32 °C (0.5 steps), OFF, clock. Swing, powerful,
  quiet (outdoor), econo, sensor, comfort, mold, timers and the DRY 0xC0 temperature byte are inferred from the library and the
  ARC466A33 / ARC466A9 / ARC452A4 captures, not seen from an A41.
- OFF frames keep the previous mode/fan/temperature bytes (V2); the unit also accepts a plain copy with bit 0 cleared.
- Weekly-timer bit: 0 = active on all 407 A41 packets (unit accepted them). 0x80 (disabled) is accepted by ARC452A4 units and expected on A41 units, unverified.
- Do not copy ESPHome `daikin` timings (no preamble, one-cell +9 %, frozen clock, byte 21 bit 3 clear, OFF = 0x00, 30 °C cap).
- Remote clock: the remote sends its real time/day on every press; a fixed value is accepted but the unit's timers will then run on that time.
- Duty cycle of the real remote is unmeasured; 33-50 % is fine for TSOP-class receivers.
- The captured remote drove a CTKM50VVMG (SG); the AU CTKM-RVMA units use the same remote per the AU manual, so identical acceptance is expected, not demonstrated.

---

## 12. Open questions

1. DRY-mode temperature byte on the A41 (expected `0xC0` per library/ESPHome/BokoEnos) — verify with the real remote.
2. Swing V/H, Powerful, outdoor Quiet, Econo, Sensor, Comfort, Mold, ON/OFF timer bit values on an A41 capture (positions inferred from siblings).
3. Whether CTKM units accept IRremoteESP8266's stock timings (29428 gaps, 3650/1623 header) as well as the captured ones — emulate the captured values regardless.
4. Whether 0x80 in byte 32 (weekly timer disabled) is accepted by A41-era units.
5. Meaning of byte 5 = 0x10, byte 31 = 0xC5, byte 33 = 0x08.
6. Real-remote carrier duty cycle (never measured by any source).

---

## 13. Provenance index

- IRremoteESP8266 (`R/IRremoteESP8266/`): `src/ir_Daikin.h:48-49, 73-138, 141-180, 278-279, 622-629, 790-804, 811, 821-822`; `src/ir_Daikin.cpp:59-107, 132-158, 161-185, 197-205, 215-470, 602-662, 3104`; `src/IRsend.cpp:99-116, 164-207, 266-297`; `src/IRsend.h:33-35, 44`; `src/IRremoteESP8266.h:1172, 1218-1222`; `src/IRutils.cpp:451-457`; `test/ir_Daikin_test.cpp:13-71, 565-582, 630-650, 690-700, 798-874`; `ReleaseNotes.md:3,20`; `library.properties:2`.
- ARC466A41 captures: `R/r3-arc466a41/smartir_1119_arc466a41.json` (SmartIR PR #1021, https://github.com/smartHomeHub/SmartIR/pull/1021), `R/r3-arc466a41/smartir_pr1021.json`; decoders `scratch/a41_robust.mjs`, `scratch/verify_1119_independent.js`, `scratch/dump_a41_vector.mjs`, `scratch/timing_stats.mjs`, `scratch/daikin_layout_verify.mjs`; decoded listing `scratch/decoded_1119.txt` (byte columns valid; its µs columns use the wrong 32.84 tick).
- Broadlink tick: python-broadlink `protocol.md` (https://raw.githubusercontent.com/mjg59/python-broadlink/master/protocol.md): "Pulse lengths in 2^-15 s units (µs * 269 / 8192 works very well)"; terminator "0x00 0x0d 0x05 ... about 102 milliseconds".
- Sibling captures: `R/github/issue_1101.json` (ARC466A33), `test/ir_Daikin_test.cpp:573-580` (ARC452A4, issue #704), `R/writeups/flipper-irdb/Daikin_FTXS25KVMA.ir:9-11` and `Daikin_FTXM95PVMA.ir`, `R/writeups/irforum/t1028_archive.txt:73` (AnalysIR), `R/writeups/hifi-remote/p82812_archive.txt:90-116`.
- Other encoders: `R/writeups/BokoEnos-daikin_ir_encoder/src/daikin_ir_encoder/_encoder.py:20-49, 194-234` and `README.md:94-125`; `R/writeups/mildsunrise-gist/daikin.py:40-77`; `R/writeups/ToniA-arduino-heatpumpir/DaikinHeatpumpIR.h:11-16`; `R/writeups/mharizanov-Daikin-AC-remote/IRremote/IRremoteInt.h:60-65`; `R/writeups/harizanov/page.txt:156-170`; `R/writeups/rdlab-cdmt/page.txt:653-664`; `R/writeups/YSYouJack-Daikin-ir-control-rpi/README.md:32-38`.
- ESPHome: `R/esphome/daikin.h:38-50`, `R/esphome/daikin.cpp:24-107, 154-179, 319-326`, `R/esphome/tests/daikin_regression_tests.cpp:28-30, 193-198, 351-378`, `R/esphome/climate_ir.mdx:56`, `R/r3-arc466a41/esphome_fr2360.json` + `_comments.json`.
- Daikin manual: `R/r3-arc466a41/daikin_au_multisplit_3P472684.txt:656-657, 685-691, 845-848, 873-874, 1423, 2476-2483` (PDF `daikin_au_multisplit_CTXM-CTKM-RVMA_3P472684.pdf`).
