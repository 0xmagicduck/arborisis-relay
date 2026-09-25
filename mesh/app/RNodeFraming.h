// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// RNodeFraming.h — how a Reticulum packet travels over LoRa between RNodes.
//
// A LoRa frame carries at most 255 bytes; a Reticulum packet on a LoRa
// interface up to 508. The RNode firmware puts one header byte in front of
// every frame: a random 4-bit sequence in the high nibble, FLAG_SPLIT in the
// low one. A packet over 254 bytes goes out as two frames with the same
// header, the first one full. Every node that means to talk to the RNodes
// (Sideband, rnsd, the Arborisis relays…) has to do exactly this; the tests
// in test/test_rnode_framing pin it down byte for byte.
//
// Pure C++, no Arduino.

#pragma once

#include <stdint.h>
#include <string.h>

namespace arb {

static const uint16_t RNODE_MTU          = 508;  // largest Reticulum packet on LoRa
static const uint16_t RNODE_FRAME_MAX    = 255;  // largest LoRa frame
static const uint8_t  RNODE_FLAG_SPLIT   = 0x01;
static const uint8_t  RNODE_SEQ_UNSET    = 0xFF;

// ── Transmit side ──────────────────────────────────────────────────────────
// Cuts `len` bytes into one or two frames. `rnd` is any random byte (only
// its high nibble is used). Returns the number of frames (0 if len is 0 or
// over RNODE_MTU); frame i is frames[i][0 .. frame_len[i]).
struct RNodeTxFrames {
  uint8_t  count = 0;
  uint8_t  frames[2][RNODE_FRAME_MAX];
  uint16_t frame_len[2] = {0, 0};
};

inline uint8_t rnodeSplit(const uint8_t* data, uint16_t len, uint8_t rnd, RNodeTxFrames& out) {
  out.count = 0;
  if (len == 0 || len > RNODE_MTU) return 0;
  uint8_t header = rnd & 0xF0;
  const uint16_t cap = RNODE_FRAME_MAX - 1;   // payload bytes per frame
  if (len > cap) header |= RNODE_FLAG_SPLIT;

  uint16_t first = len > cap ? cap : len;
  out.frames[0][0] = header;
  memcpy(&out.frames[0][1], data, first);
  out.frame_len[0] = first + 1;
  out.count = 1;

  if (len > cap) {
    // RNode starts the second frame only when bytes remain, so a 254-byte
    // packet is one frame and a 508-byte one two full frames: never an
    // empty trailer.
    uint16_t rest = len - first;
    out.frames[1][0] = header;
    memcpy(&out.frames[1][1], data + first, rest);
    out.frame_len[1] = rest + 1;
    out.count = 2;
  }
  return out.count;
}

// ── Receive side ───────────────────────────────────────────────────────────
// Feeds received LoRa frames in; a complete Reticulum packet comes out when
// push() returns true. The rules are the RNode firmware's (receive_callback):
//
//   split, nothing pending        → first half: remember seq, buffer it
//   split, same seq as pending    → second half: packet complete
//   split, other seq              → a new first half replaces the pending one
//   not split                     → a whole packet; a pending half is kept
//                                   (RTNode's side channel), so a short packet
//                                   between the two halves of a long one does
//                                   not cost the long one
//
// plus the empty trailing frame some RNode versions emit after a split whose
// size is a multiple of the frame payload: recognised by its sequence and
// dropped. A pending half older than `stale_ms` is dropped too.
class RNodeReassembler {
public:
  struct Packet {
    const uint8_t* data;
    uint16_t len;
  };

  explicit RNodeReassembler(uint32_t stale_ms = 5000) : _stale_ms(stale_ms) { reset(); }

  void reset() { _seq = RNODE_SEQ_UNSET; _last_seq = RNODE_SEQ_UNSET; _len = 0; _started_ms = 0; }

  bool pending() const { return _seq != RNODE_SEQ_UNSET; }
  uint8_t pendingSeq() const { return _seq; }

  // Returns true when `out` holds a complete packet; out.data stays valid
  // until the next push().
  bool push(const uint8_t* frame, uint16_t frame_len, uint32_t now_ms, Packet& out) {
    if (frame_len == 0) return false;
    if (pending() && (uint32_t)(now_ms - _started_ms) > _stale_ms) _seq = RNODE_SEQ_UNSET;

    const uint8_t header = frame[0];
    const uint8_t seq = header >> 4;
    const bool split = (header & RNODE_FLAG_SPLIT) != 0;
    const uint8_t* body = frame + 1;
    const uint16_t body_len = frame_len - 1;

    if (split) {
      if (!pending()) {
        if (body_len == 0 && seq == _last_seq) return false;   // empty trailer
        return startHalf(seq, body, body_len, now_ms);
      }
      if (seq == _seq) {
        if (_len + body_len > RNODE_MTU) { _seq = RNODE_SEQ_UNSET; return false; }
        memcpy(_buf + _len, body, body_len);
        _len += body_len;
        _last_seq = seq;
        _seq = RNODE_SEQ_UNSET;
        out.data = _buf;
        out.len = _len;
        return _len > 0;
      }
      return startHalf(seq, body, body_len, now_ms);
    }

    // Not split: a whole packet on its own.
    if (body_len == 0) return false;
    if (pending()) {
      memcpy(_side, body, body_len);
      out.data = _side;
    } else {
      memcpy(_buf, body, body_len);
      _len = 0;
      out.data = _buf;
    }
    out.len = body_len;
    return true;
  }

private:
  bool startHalf(uint8_t seq, const uint8_t* body, uint16_t body_len, uint32_t now_ms) {
    _seq = seq;
    _started_ms = now_ms;
    memcpy(_buf, body, body_len);
    _len = body_len;
    return false;
  }

  uint32_t _stale_ms;
  uint8_t  _seq, _last_seq;
  uint16_t _len;
  uint32_t _started_ms;
  uint8_t  _buf[RNODE_MTU];
  uint8_t  _side[RNODE_FRAME_MAX];
};

}  // namespace arb
