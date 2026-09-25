// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// Kiss.h — KISS framing (FEND/FESC), as RNode and Reticulum use it on a
// serial line or a BLE UART. Pure C++, no Arduino.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace arb {
namespace kiss {

static const uint8_t FEND  = 0xC0;
static const uint8_t FESC  = 0xDB;
static const uint8_t TFEND = 0xDC;
static const uint8_t TFESC = 0xDD;

// Byte sink: whatever carries the frames (USB CDC, BLE UART, a test buffer).
class Sink {
public:
  virtual ~Sink() {}
  virtual void put(uint8_t b) = 0;
  virtual void flush() {}
};

inline void putEscaped(Sink& s, uint8_t b) {
  if (b == FEND)      { s.put(FESC); s.put(TFEND); }
  else if (b == FESC) { s.put(FESC); s.put(TFESC); }
  else                { s.put(b); }
}

// One whole frame: FEND cmd <escaped payload> FEND.
inline void writeFrame(Sink& s, uint8_t cmd, const uint8_t* payload, size_t len) {
  s.put(FEND);
  s.put(cmd);
  for (size_t i = 0; i < len; i++) putEscaped(s, payload[i]);
  s.put(FEND);
}

// Streaming decoder. Bytes outside a frame are reported as text (the serial
// line is shared with a line-based console), bytes inside are unescaped
// into the frame buffer; feed() returns KISS_FRAME when one is complete.
template <size_t CAP>
class Decoder {
public:
  // Prefixed: board libraries define plain words as macros (meshsolar's
  // logger.h, for the Heltec MeshSolar, has `#define NONE 1`), and a macro
  // does not care about scopes.
  enum Result { KISS_NONE, KISS_FRAME, KISS_TEXT };

  Decoder() { reset(); }

  void reset() { _in_frame = false; _escape = false; _len = 0; _overflow = false; }

  bool inFrame() const { return _in_frame; }

  // For KISS_TEXT, the byte is in `text_byte`. For KISS_FRAME, the frame is
  // command() / payload() / payloadLen().
  Result feed(uint8_t b, uint8_t& text_byte) {
    if (b == FEND) {
      if (_in_frame && _len > 0 && !_overflow) {
        // A frame ends here; FEND also opens the next one (back-to-back
        // frames share their delimiter, as RNS writes them).
        _done_len = _len;
        _in_frame = true; _escape = false; _len = 0; _overflow = false;
        return KISS_FRAME;
      }
      _in_frame = true; _escape = false; _len = 0; _overflow = false;
      return KISS_NONE;
    }
    if (!_in_frame) { text_byte = b; return KISS_TEXT; }

    if (b == FESC) { _escape = true; return KISS_NONE; }
    if (_escape) {
      if (b == TFEND) b = FEND;
      else if (b == TFESC) b = FESC;
      _escape = false;
    }
    if (_len < CAP) _buf[_len++] = b; else _overflow = true;
    return KISS_NONE;
  }

  // A closing FEND leaves the decoder inside the next frame, empty — RNS
  // writes its detect burst as FEND a FEND b FEND c FEND, one delimiter
  // between two frames. On a line shared with a console, the owner calls
  // abandonFrame() when such an empty frame has seen nothing for a while,
  // so that a line typed after a KISS session is read as text again.
  bool openAndEmpty() const { return _in_frame && _len == 0 && !_escape; }
  void abandonFrame() { _in_frame = false; _escape = false; _len = 0; }

  uint8_t command() const { return _buf[0]; }
  const uint8_t* payload() const { return _buf + 1; }
  size_t payloadLen() const { return _done_len > 0 ? _done_len - 1 : 0; }

private:
  bool _in_frame, _escape, _overflow;
  size_t _len = 0, _done_len = 0;
  uint8_t _buf[CAP];
};

}  // namespace kiss
}  // namespace arb
