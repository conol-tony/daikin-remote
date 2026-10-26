// 38 kHz infrared transmitter built on the ESP32 RMT peripheral.
//
// The RMT hardware generates the carrier and times every mark/space in
// hardware, so the CPU (display updates, serial, interrupts) cannot jitter the
// waveform. Works on ESP32 and ESP32-S3 with arduino-esp32 2.x (IDF 4.4) and
// 3.x (IDF 5.x).
#pragma once

#include <stddef.h>
#include <stdint.h>

class IrTransmitter {
 public:
  // gpio: output pin driving the IR LED (through a transistor).
  // carrierHz / dutyPercent: carrier modulation applied during marks.
  bool begin(int gpio, uint32_t carrierHz = 38000, uint8_t dutyPercent = 33);

  // Send a mark/space sequence in microseconds. Even indices are marks
  // (carrier on), odd indices are spaces (carrier off). Blocks until the
  // whole frame has left the pin. Returns false if not initialised.
  bool send(const uint32_t* timings, size_t count);

  bool ready() const { return ready_; }
  int gpio() const { return gpio_; }

 private:
  int gpio_ = -1;
  bool ready_ = false;
};
