#include "ir_tx.h"

#include <Arduino.h>

#if ESP_ARDUINO_VERSION_MAJOR >= 3
// arduino-esp32 3.x (ESP-IDF 5): the legacy driver/rmt.h cannot coexist with
// the core's own RMT HAL, so use the core's rmtInit/rmtWrite API instead.
#include "esp32-hal-rmt.h"
#else
#include "driver/rmt.h"
#endif

namespace {

constexpr uint32_t kMaxTicks = 32767;  // RMT durations are 15 bits wide.
constexpr size_t kMaxSymbols = 512;    // 35 bytes * 8 bits + headers fits easily.

#if ESP_ARDUINO_VERSION_MAJOR >= 3
using Symbol = rmt_data_t;
#else
using Symbol = rmt_item32_t;
constexpr rmt_channel_t kChannel = RMT_CHANNEL_0;  // TX capable on ESP32 and ESP32-S3.
#endif

// Pack a mark/space list into RMT symbols. Each symbol carries two pulses
// (level + duration). Pulses longer than 15 bits are split across symbols.
size_t pack(const uint32_t* timings, size_t count, Symbol* out, size_t maxOut) {
  size_t n = 0;       // symbols written
  bool half = false;  // true when symbol n already has its first pulse

  auto push = [&](uint32_t level, uint32_t ticks) -> bool {
    if (!half) {
      if (n >= maxOut) return false;
      out[n].val = 0;
      out[n].duration0 = ticks;
      out[n].level0 = level;
      half = true;
    } else {
      out[n].duration1 = ticks;
      out[n].level1 = level;
      half = false;
      n++;
    }
    return true;
  };

  for (size_t i = 0; i < count; i++) {
    const uint32_t level = (i % 2 == 0) ? 1 : 0;  // even = mark (carrier on)
    uint32_t remaining = timings[i];
    if (remaining == 0) continue;
    while (remaining > 0) {
      const uint32_t chunk = remaining > kMaxTicks ? kMaxTicks : remaining;
      if (!push(level, chunk)) return 0;
      remaining -= chunk;
    }
  }
  // Terminate: a zero-length pulse ends the transmission.
  if (half) {
    out[n].duration1 = 0;
    out[n].level1 = 0;
    n++;
  } else {
    if (n >= maxOut) return 0;
    out[n].val = 0;
    n++;
  }
  return n;
}

Symbol symbols[kMaxSymbols];

}  // namespace

bool IrTransmitter::begin(int gpio, uint32_t carrierHz, uint8_t dutyPercent) {
  gpio_ = gpio;
  ready_ = false;
  if (gpio < 0) return false;
  pinMode(gpio, OUTPUT);
  digitalWrite(gpio, LOW);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // 1 MHz tick -> 1 µs resolution.
  if (!rmtInit(gpio, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000)) return false;
  // carrier_level=false: carrier rides on the high (mark) level.
  if (!rmtSetCarrier(gpio, true, false, carrierHz, dutyPercent / 100.0f)) return false;
#else
  rmt_config_t cfg = {};
  cfg.rmt_mode = RMT_MODE_TX;
  cfg.channel = kChannel;
  cfg.gpio_num = static_cast<gpio_num_t>(gpio);
  cfg.clk_div = 80;  // 80 MHz APB / 80 = 1 MHz -> 1 µs per tick.
  cfg.mem_block_num = 1;
  cfg.flags = 0;
  cfg.tx_config.carrier_freq_hz = carrierHz;
  cfg.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  cfg.tx_config.carrier_duty_percent = dutyPercent;
  cfg.tx_config.carrier_en = true;
  cfg.tx_config.loop_en = false;
  cfg.tx_config.idle_output_en = true;
  if (rmt_config(&cfg) != ESP_OK) return false;
  if (rmt_driver_install(kChannel, 0, 0) != ESP_OK) return false;
#endif
  ready_ = true;
  return true;
}

bool IrTransmitter::send(const uint32_t* timings, size_t count) {
  if (!ready_ || count == 0) return false;
  const size_t n = pack(timings, count, symbols, kMaxSymbols);
  if (n == 0) return false;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return rmtWrite(gpio_, symbols, n, RMT_WAIT_FOR_EVER);
#else
  // wait_tx_done=true blocks until the last symbol has been shifted out; the
  // driver streams frames longer than one memory block from its ISR.
  return rmt_write_items(kChannel, symbols, n, true) == ESP_OK;
#endif
}
