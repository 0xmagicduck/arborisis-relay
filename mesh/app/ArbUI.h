// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// ArbUI.h — the board's display and user button.
//
// With a display (any MeshCore display driver: SSD1306, SH1106, ST7789,
// e-ink…): four pages — overview, MeshCore, Reticulum, radio — turned by a
// short press; the display goes dark after the configured timeout and wakes
// on a press. A long press opens the mode menu: short presses move through
// the modes, a long press on one saves it and restarts the board in it (a
// long press on the current mode, or 15 s without a press, closes the menu).
//
// Without a display, on the boards whose button polarity the MeshCore board
// file states (USER_BTN_PRESSED: T1000-E, MeshTracker X1, R1 Neo…), a triple
// press switches to the next mode and restarts; the console says which.

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
  void renderMenu();
  void button(int ev, uint32_t now);
  void applyMode(uint8_t mode);
  bool _menu = false;
  uint8_t _menu_sel = 0;
  uint32_t _menu_until = 0;
  uint8_t _page = 0;
  uint32_t _next_refresh = 0;
  uint32_t _off_at = 0;
  bool _on = false;
};

extern ArbUI ui;

}  // namespace arb
