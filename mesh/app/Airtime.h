// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// Airtime.h — how much of the channel this node has used, over a short
// window (15 s) and a long one (1 h, the window of the EU duty-cycle rule).
// Pure C++, no Arduino.

#pragma once

#include <stdint.h>
#include <string.h>

namespace arb {

class AirtimeMeter {
public:
  static const uint32_t BIN_MS   = 15000;          // one bin = the short window
  static const uint16_t BINS     = 240;            // 240 × 15 s = 1 h

  AirtimeMeter() { reset(0); }

  void reset(uint32_t now_ms) {
    memset(_bins, 0, sizeof(_bins));
    _bin_started = now_ms;
    _cur = 0;
    _total_ms = 0;
  }

  // Record `ms` of transmission ending now.
  void add(uint32_t now_ms, uint32_t ms) {
    roll(now_ms);
    uint32_t v = (uint32_t)_bins[_cur] + ms;
    _bins[_cur] = v > 0xFFFF ? 0xFFFF : (uint16_t)v;   // a bin holds ≤ 15 000 ms
    _total_ms += ms;
  }

  // Share of the last hour spent transmitting, 0.0 … 1.0.
  float longTerm(uint32_t now_ms) {
    roll(now_ms);
    uint32_t sum = 0;
    for (uint16_t i = 0; i < BINS; i++) sum += _bins[i];
    return (float)sum / (float)(BINS * BIN_MS);
  }

  // Share of the current and previous bin, the way RNode averages it.
  float shortTerm(uint32_t now_ms) {
    roll(now_ms);
    uint16_t prev = _cur == 0 ? BINS - 1 : _cur - 1;
    uint32_t span = BIN_MS + (uint32_t)(now_ms - _bin_started);
    if (span == 0) return 0.0f;
    return (float)(_bins[_cur] + _bins[prev]) / (float)span;
  }

  // Would `ms` more of transmission push the long-term share above
  // `limit` (0.0 … 1.0)? A limit of 0 means no limit.
  bool wouldExceed(uint32_t now_ms, uint32_t ms, float limit) {
    if (limit <= 0.0f) return false;
    roll(now_ms);
    uint32_t sum = ms;
    for (uint16_t i = 0; i < BINS; i++) sum += _bins[i];
    return (float)sum > limit * (float)(BINS * BIN_MS);
  }

  uint32_t totalMs() const { return _total_ms; }

private:
  void roll(uint32_t now_ms) {
    uint32_t elapsed = now_ms - _bin_started;
    if (elapsed < BIN_MS) return;
    uint32_t steps = elapsed / BIN_MS;
    if (steps >= BINS) {
      memset(_bins, 0, sizeof(_bins));
      _cur = 0;
    } else {
      for (uint32_t s = 0; s < steps; s++) {
        _cur = (_cur + 1) % BINS;
        _bins[_cur] = 0;
      }
    }
    _bin_started += steps * BIN_MS;
  }

  uint16_t _bins[BINS];
  uint32_t _bin_started;
  uint16_t _cur;
  uint32_t _total_ms;
};

}  // namespace arb
