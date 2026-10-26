// Serial command interpreter. One command per line, e.g. "temp 24.5".
#pragma once

#include <Arduino.h>

#include "console.h"
#include "controller.h"

class CommandHandler {
 public:
  CommandHandler(AcController& ac, Console& console) : ac_(ac), console_(console) {}

  // Returns true if the command changed the A/C state.
  bool handle(const String& line);

 private:
  void help();
  void ok(const char* what);
  void err(const char* what);
  void printRaw();

  AcController& ac_;
  Console& console_;
};
