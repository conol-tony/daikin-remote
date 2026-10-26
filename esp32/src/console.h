// Line-oriented serial console that listens on every available port:
// native USB-CDC (Serial on the S3 when CDC-on-boot is enabled) and UART0
// (Serial0 / the external USB-serial bridge). Output goes to all of them.
#pragma once

#include <Arduino.h>

class Console {
 public:
  void begin(uint32_t baud) {
    Serial.begin(baud);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(baud);
#endif
  }

  // Returns true once a complete line (terminated by \n or \r) is available.
  bool readLine(String& out) {
    if (pump(Serial, out)) return true;
#if ARDUINO_USB_CDC_ON_BOOT
    if (pump(Serial0, out)) return true;
#endif
    return false;
  }

  void print(const String& s) {
    Serial.print(s);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.print(s);
#endif
  }

  void println(const String& s) { print(s + "\n"); }

  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    print(buf);
  }

 private:
  bool pump(Stream& port, String& out) {
    while (port.available()) {
      const char c = static_cast<char>(port.read());
      if (c == '\n' || c == '\r') {
        if (line_.length() == 0) continue;  // swallow CRLF pairs / blank lines
        out = line_;
        line_ = "";
        return true;
      }
      if (line_.length() < 120) line_ += c;
    }
    return false;
  }

  String line_;
};
