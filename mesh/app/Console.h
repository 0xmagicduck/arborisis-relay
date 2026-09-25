// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// Console.h — the USB serial port, shared by three kinds of traffic:
//
//   • KISS frames: an RNode host (rnsd, Sideband over USB…) — every byte
//     between FEND delimiters goes to RNodeHost;
//   • `arb …` lines: the Arborisis settings (mode, Reticulum channel, duty
//     cycle…);
//   • any other line: MeshCore's repeater CLI, unchanged (`ver`, `set freq`,
//     `neighbors`, `advert`…), answered with MeshCore's "  -> " prefix, so the
//     MeshCore web tools that drive a repeater over serial keep working.
//
// Text is only ever written between two complete KISS frames, and a KISS
// host ignores bytes outside frames.

#pragma once

#include <Arduino.h>
#include "Kiss.h"
#include "RNodeHost.h"

namespace arb {

// KISS frames to the USB port, one whole frame per write, dropped rather
// than blocking when no host holds the port open (native USB) — a board
// that nobody listens to must not stall its radio loop.
class SerialSink : public kiss::Sink {
public:
  void put(uint8_t b) override {
    if (_n < sizeof(_buf)) _buf[_n++] = b; else _overflow = true;
  }
  void flush() override {
    if (!_overflow && _n > 0 && (bool)Serial) Serial.write(_buf, _n);
    _n = 0;
    _overflow = false;
  }
private:
  uint8_t _buf[2 * 512 + 8];
  size_t _n = 0;
  bool _overflow = false;
};

class Console {
public:
  void begin();
  void loop();

  // Status lines, shared with the display.
  static void statusText(char* out, size_t n);
  static void statusJson(char* out, size_t n);

private:
  void textByte(uint8_t c);
  void runLine(char* line);
  bool arbCommand(char* args, char* reply, size_t n);

  kiss::Decoder<600> _kiss;
  char _line[160];
  size_t _len = 0;
  uint32_t _last_rx = 0;
  bool _had_dtr = false;
};

extern Console console;

}  // namespace arb
