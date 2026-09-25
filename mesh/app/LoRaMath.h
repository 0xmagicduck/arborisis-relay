// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// LoRaMath.h — the arithmetic both protocols need about a LoRa channel:
// symbol time, time on air, and the preamble each network puts in front of
// a packet. Pure functions, no Arduino: the host tests run them as they are.

#pragma once

#include <stdint.h>
#include <math.h>

namespace arb {

// One LoRa channel as the arbiter programs it into the radio.
struct LoRaChannel {
  uint32_t freq_hz  = 0;
  uint32_t bw_hz    = 0;
  uint8_t  sf       = 0;   // 5 … 12
  uint8_t  cr       = 5;   // coding rate denominator: 5 = 4/5 … 8 = 4/8
  int8_t   txp_dbm  = 0;
  uint16_t preamble = 0;   // symbols

  bool valid() const {
    return freq_hz >= 137000000UL && freq_hz <= 3000000000UL &&
           bw_hz >= 7800 && bw_hz <= 1625000 &&
           sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8;
  }

  // Two channels a receiver cannot tell apart: the same frequency and the
  // same modulation. The coding rate is not part of it — it travels in the
  // explicit LoRa header, so a receiver decodes any of them — and neither is
  // the power.
  bool samePhy(const LoRaChannel& o) const {
    return freq_hz == o.freq_hz && bw_hz == o.bw_hz && sf == o.sf;
  }
};

inline float symbolTimeMs(uint32_t bw_hz, uint8_t sf) {
  return bw_hz == 0 ? 0.0f : (float)(1UL << sf) * 1000.0f / (float)bw_hz;
}

// Semtech: low data rate optimisation is mandatory above 16 ms per symbol.
inline bool lowDataRateOptimize(uint32_t bw_hz, uint8_t sf) {
  return symbolTimeMs(bw_hz, sf) > 16.0f;
}

inline float preambleTimeMs(const LoRaChannel& c) {
  return (float)c.preamble * symbolTimeMs(c.bw_hz, c.sf);
}

// Raw LoRa bit rate, bit/s, as RNode computes it (and reports it to RNS).
inline uint32_t loraBitrate(uint32_t bw_hz, uint8_t sf, uint8_t cr) {
  if (bw_hz == 0 || sf == 0 || cr == 0) return 0;
  return (uint32_t)((float)sf * ((4.0f / (float)cr) / ((float)(1UL << sf) / ((float)bw_hz / 1000.0f))) * 1000.0f);
}

// Time on air of one LoRa frame (Semtech AN1200.13), explicit header,
// CRC on, in microseconds.
inline uint32_t timeOnAirUs(uint32_t bw_hz, uint8_t sf, uint8_t cr, uint16_t preamble, uint16_t payload_len,
                            bool crc = true, bool explicit_header = true) {
  if (bw_hz == 0) return 0;
  const float tsym_us = (float)(1UL << sf) * 1e6f / (float)bw_hz;
  const int de = lowDataRateOptimize(bw_hz, sf) ? 1 : 0;
  const int ih = explicit_header ? 0 : 1;
  // SF5/SF6 on the SX126x use a 6.25 symbol sync and no +8 bias quirk worth
  // modelling here; the formula stays within a symbol of the chip for them.
  float num = 8.0f * payload_len - 4.0f * sf + 28.0f + 16.0f * (crc ? 1 : 0) - 20.0f * ih;
  float den = 4.0f * (sf - 2 * de);
  float payload_syms = 8.0f;
  if (num > 0) payload_syms += ceilf(num / den) * (float)cr;
  const float total_syms = (float)preamble + 4.25f + payload_syms;
  return (uint32_t)(total_syms * tsym_us);
}

inline uint32_t timeOnAirMs(const LoRaChannel& c, uint16_t payload_len) {
  return (timeOnAirUs(c.bw_hz, c.sf, c.cr, c.preamble, payload_len) + 999) / 1000;
}

// ── Reticulum over LoRa (RNode) ────────────────────────────────────────────
// The RNode firmware sizes its preamble to ~24 ms (6 ms above 30 kbit/s),
// never under 18 symbols. A receiver copes with a longer preamble than it
// was told, not with a shorter one, so a node that means to be heard by the
// RNodes around it sends exactly theirs.
static const uint16_t RNODE_PREAMBLE_SYMBOLS_MIN = 18;
static const float    RNODE_PREAMBLE_TARGET_MS   = 24.0f;
static const float    RNODE_PREAMBLE_FAST_DELTA  = 18.0f;
static const uint32_t RNODE_FAST_THRESHOLD_BPS   = 30000;

inline uint16_t rnodePreambleSymbols(uint32_t bw_hz, uint8_t sf, uint8_t cr) {
  const float tsym = symbolTimeMs(bw_hz, sf);
  if (tsym <= 0.0f) return RNODE_PREAMBLE_SYMBOLS_MIN;
  float target_ms = RNODE_PREAMBLE_TARGET_MS;
  if (loraBitrate(bw_hz, sf, cr) > RNODE_FAST_THRESHOLD_BPS) target_ms -= RNODE_PREAMBLE_FAST_DELTA;
  float syms = target_ms / tsym;
  if (syms < RNODE_PREAMBLE_SYMBOLS_MIN) return RNODE_PREAMBLE_SYMBOLS_MIN;
  return (uint16_t)ceilf(syms);
}

// RNode CSMA slot: 12 symbols, clamped to 24 … 100 ms (6 ms floor when fast).
inline uint32_t rnodeCsmaSlotMs(uint32_t bw_hz, uint8_t sf, uint8_t cr) {
  const bool fast = loraBitrate(bw_hz, sf, cr) > RNODE_FAST_THRESHOLD_BPS;
  float slot = symbolTimeMs(bw_hz, sf) * 12.0f;
  if (slot > 100.0f) slot = 100.0f;
  if (slot < 24.0f) slot = fast ? 6.0f : 24.0f;
  return (uint32_t)slot;
}

// ── MeshCore ───────────────────────────────────────────────────────────────
// MeshCore (RadioLibWrapper::preambleLengthForSF) lengthens the preamble at
// low spreading factors: 32 symbols up to SF8, 16 above.
inline uint16_t meshcorePreambleSymbols(uint8_t sf) { return sf <= 8 ? 32 : 16; }

// SX126x/SX127x/LR11x0 bandwidth steps. RNS compares the bandwidth an RNode
// echoes with the one it asked for, so a requested value is snapped to the
// closest step the chip really has, and that is what is echoed.
inline uint32_t snapBandwidth(uint32_t bw_hz) {
  static const uint32_t steps[] = { 7800, 10400, 15600, 20800, 31250, 41700, 62500,
                                    125000, 250000, 500000, 812500, 1625000 };
  uint32_t best = steps[0];
  uint32_t best_d = 0xFFFFFFFFUL;
  for (uint32_t s : steps) {
    uint32_t d = s > bw_hz ? s - bw_hz : bw_hz - s;
    if (d < best_d) { best = s; best_d = d; }
  }
  return best;
}

}  // namespace arb
