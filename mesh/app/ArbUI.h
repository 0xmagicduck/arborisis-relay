// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// ArbUI.h — the board's display, when it has one (any MeshCore display
// driver: SSD1306, SH1106, ST7789, e-ink…): four pages — overview,
// MeshCore, Reticulum, radio — turned by the user button; the display goes
// dark after the configured timeout and wakes on a press.

#pragma once

#include <Arduino.h>

namespace arb {

class ArbUI {
public:
  void begin();
  void loop();
  void splash(const char* line);

private:
  void render();
  uint8_t _page = 0;
  uint32_t _next_refresh = 0;
  uint32_t _off_at = 0;
  bool _on = false;
};

extern ArbUI ui;

}  // namespace arb
