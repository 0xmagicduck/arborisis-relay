// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// BleLink.h — the RNode host protocol over Bluetooth LE, for Sideband.
//
// Sideband (and RNS with `port = ble://`) looks for a bonded device named
// "RNode XXXX" offering the Nordic UART Service, and speaks the same KISS
// frames as over USB. BleLink is that service: a byte stream in, KISS
// frames out as notifications. Pairing is by passkey (MITM, bonded): the
// six digits are on the display and on the console (`arb ble`).
//
// ESP32 family: Arduino's BLE library (Bluedroid). nRF52840: Bluefruit.
// Compiled when ARB_WITH_BLE is set (see platformio.ini).

#pragma once

#include <Arduino.h>
#include "Kiss.h"

#ifndef ARB_WITH_BLE
  #define ARB_WITH_BLE 0
#endif

namespace arb {

class BleLink : public kiss::Sink {
public:
  // `name` is the advertised name ("RNode 1A2B"); `pin` six digits.
  bool begin(const char* name, uint32_t pin);
  bool running() const { return _running; }
  bool connected();
  int available();
  int read();

  // kiss::Sink: bytes of one frame, then flush() sends them.
  void put(uint8_t b) override;
  void flush() override;

  // Called from the stack's callbacks.
  void onRx(const uint8_t* data, size_t len);
  void onConnect() { _connected = true; }
  void onDisconnect() { _connected = false; _authenticated = false; _rx_head = _rx_tail = 0; }
  void onAuthenticated(bool ok) { _authenticated = ok; }
  bool authenticated() const { return _authenticated; }

private:
  static const size_t RX_CAP = 2048;
  static const size_t TX_CAP = 2 * 512 + 8;
  uint8_t _rx[RX_CAP];
  volatile size_t _rx_head = 0, _rx_tail = 0;
  uint8_t _tx[TX_CAP];
  size_t _tx_n = 0;
  bool _running = false;
  volatile bool _connected = false;
  volatile bool _authenticated = false;
};

}  // namespace arb
