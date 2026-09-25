// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// ChannelPlan.h — how one transceiver listens to two LoRa channels.
//
// A LoRa radio receives on one channel at a time. MeshCore and Reticulum
// normally live on two (in Belgium: 869.618 MHz / 62.5 kHz for MeshCore,
// 869.525 MHz / 125 kHz for Reticulum). What makes sharing possible is the
// preamble: before any packet, the sender transmits a run of identical
// symbols, and a receiver that tunes in during that run still catches the
// packet. So:
//
//   • the radio *listens* continuously on the channel with the shorter
//     preamble (A) — it cannot afford to be away long;
//   • every `peek_every_ms` it *peeks* at the other one (B) with a Channel
//     Activity Detection — a few symbols long — and comes straight back;
//   • a peek that detects a preamble stays on B to receive the packet.
//
// The peek period is a third of B's preamble, so any packet on B is seen by
// at least two peeks; a peek costs two retunes and a CAD, which must be
// shorter than A's preamble minus what A's receiver needs to lock (six
// symbols), or a packet on A that starts during a peek is lost — the plan
// then says `degraded`. With the Belgian channels: A = Reticulum (18
// symbols × 2.05 ms = 36.9 ms), B = MeshCore (32 × 4.1 ms = 131 ms), peek
// every 43 ms for ~17 ms: not degraded.
//
// When both protocols use the very same channel (frequency, bandwidth, SF)
// there is nothing to peek at: one receiver, and each frame is sorted by
// its content (see classify() in RadioArbiter.cpp).
//
// Pure C++; the host tests exercise it.

#pragma once

#include <stdint.h>
#include "LoRaMath.h"

namespace arb {

enum Proto : int8_t { PROTO_NONE = -1, PROTO_MC = 0, PROTO_RNS = 1 };

struct ChannelPlan {
  int8_t   listen = PROTO_NONE;   // channel the receiver sits on
  int8_t   peek   = PROTO_NONE;   // channel visited by CAD, if any
  bool     shared = false;        // both protocols on one channel
  bool     degraded = false;      // a peek is longer than the listen channel can spare
  uint32_t peek_every_ms = 0;
  uint32_t peek_cost_ms  = 0;
  uint16_t listen_preamble = 0;   // RX preamble setting on the listen channel
};

// `retune_ms`: time to reprogram frequency and modulation (SPI + PLL lock),
// a couple of milliseconds on the SX126x without image recalibration.
inline ChannelPlan makePlan(bool mc_on, const LoRaChannel& mc, bool rns_on, const LoRaChannel& rns,
                            uint32_t retune_ms = 2) {
  ChannelPlan p;
  if (!mc_on && !rns_on) return p;
  if (mc_on != rns_on) {
    p.listen = mc_on ? PROTO_MC : PROTO_RNS;
    p.listen_preamble = mc_on ? mc.preamble : rns.preamble;
    return p;
  }
  if (mc.samePhy(rns)) {
    p.shared = true;
    p.listen = PROTO_MC;
    p.listen_preamble = mc.preamble < rns.preamble ? mc.preamble : rns.preamble;
    return p;
  }

  const float pre_mc = preambleTimeMs(mc);
  const float pre_rns = preambleTimeMs(rns);
  const bool rns_first = pre_rns <= pre_mc;
  const LoRaChannel& a = rns_first ? rns : mc;
  const LoRaChannel& b = rns_first ? mc : rns;
  p.listen = rns_first ? PROTO_RNS : PROTO_MC;
  p.peek = rns_first ? PROTO_MC : PROTO_RNS;
  p.listen_preamble = a.preamble;

  const float tsym_a = symbolTimeMs(a.bw_hz, a.sf);
  const float tsym_b = symbolTimeMs(b.bw_hz, b.sf);
  // CAD: two symbols on the SX126x up to SF8, four above; plus about one
  // symbol of processing before the chip raises CAD_DONE.
  const float cad_syms = (b.sf <= 8 ? 2.0f : 4.0f) + 1.0f;
  const float cost = 2.0f * retune_ms + cad_syms * tsym_b + 1.0f;
  p.peek_cost_ms = (uint32_t)ceilf(cost);

  float every = preambleTimeMs(b) / 3.0f;
  const float floor_ms = 2.0f * cost;      // never spend more than half the time peeking
  if (every < floor_ms) every = floor_ms;
  if (every > 1000.0f) every = 1000.0f;
  p.peek_every_ms = (uint32_t)every;

  const float spare = preambleTimeMs(a) - 6.0f * tsym_a;
  p.degraded = cost > spare || every > preambleTimeMs(b) / 2.0f;
  return p;
}

}  // namespace arb
