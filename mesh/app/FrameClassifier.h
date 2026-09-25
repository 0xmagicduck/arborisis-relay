// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// FrameClassifier.h — telling a Reticulum frame from a MeshCore one when
// both networks share one LoRa channel.
//
// Neither protocol tags its frames, and both use the LoRa sync word 0x12,
// so a shared channel can only be sorted by content. Reticulum frames are
// checked first, against the structure every RNode frame has:
//
//   byte 0  RNode header: sequence nibble, and in the low nibble only
//           FLAG_SPLIT (0x01) — bits 1–3 are always zero
//   byte 1  Reticulum flags: IFAC (bit 7) is off on a LoRa interface without
//           access codes; header type (bit 6) equals transport type (bit 4) —
//           HEADER_2 packets are the ones in transport; an announce (packet
//           type 1) goes to a SINGLE destination (type 0)
//   byte 2  hop count, small
//   then    16 bytes of destination hash (32 for HEADER_2) and a context byte
//           from Reticulum's short list (0x00–0x0E, 0xFA–0xFF); an announce
//           carries a key, two hashes and a signature: 148 bytes at least
//
// A MeshCore flood whose first byte happens to look like an RNode header
// (route flood, payload REQ, ADVERT or PATH) passes the rest about once in
// a hundred; a Reticulum frame always does, so Reticulum never loses a
// frame to MeshCore. The second half of a split Reticulum packet has no Reticulum
// header of its own: it is recognised by the sequence the first half left
// pending. Everything else is MeshCore's.
//
// This is why a dedicated channel per protocol is the recommended setup.
// Pure C++.

#pragma once

#include <stdint.h>

namespace arb {

static const uint8_t RNS_MAX_PLAUSIBLE_HOPS = 32;

inline bool looksLikeRNodeFrame(const uint8_t* f, uint16_t len, bool split_pending, uint8_t pending_seq) {
  if (len < 2) return false;
  const uint8_t hdr = f[0];
  if (hdr & 0x0E) return false;
  const bool split = hdr & 0x01;
  if (split && split_pending && (hdr >> 4) == pending_seq) return true;   // second half
  if (len < 3) return false;

  const uint8_t flags = f[1];
  const uint8_t hops = f[2];
  const bool ifac = flags & 0x80;
  const uint8_t header_type = (flags >> 6) & 1;
  const uint8_t transport_type = (flags >> 4) & 1;
  const uint8_t dest_type = (flags >> 2) & 3;
  const uint8_t packet_type = flags & 3;
  if (ifac) return false;
  if (header_type != transport_type) return false;
  if (hops > RNS_MAX_PLAUSIBLE_HOPS) return false;
  if (packet_type == 1 && dest_type != 0) return false;
  const uint16_t ctx_at = 1 + 2 + (header_type ? 32 : 16);
  if (len < ctx_at + 1) return false;
  const uint8_t ctx = f[ctx_at];
  if (!(ctx <= 0x0E || ctx >= 0xFA)) return false;
  if (packet_type == 1 && len < ctx_at + 1 + 148) return false;
  return true;
}

}  // namespace arb
