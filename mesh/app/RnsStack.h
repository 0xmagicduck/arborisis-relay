// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// RnsStack.h — microReticulum on the board, behind a plain interface.
//
// microReticulum and MeshCore cannot share a translation unit: MeshCore's
// MeshCore.h defines SEED_SIZE as a macro, which breaks the SEED_SIZE
// constant of the Crypto library's RNG.h that microReticulum includes. So
// the stack lives alone in RnsStack.cpp, and everything else talks to it
// through these functions, with neither library's headers in sight.

#pragma once

#include <stdint.h>

namespace arb {
namespace rns_stack {

typedef bool (*SendFn)(const uint8_t* data, uint16_t len);

struct Params {
  bool log = false;
  bool transport = true;       // forward for others
  uint16_t path_table = 0;     // 0: firmware default for the platform
  uint32_t bitrate = 0;        // LoRa bit rate, for Reticulum's timing
};

// Starts Reticulum with one LoRa interface; `send` queues a packet for LoRa.
bool start(const Params& p, SendFn send);
bool running();
void loop();
// A packet heard on LoRa (reassembled).
void incoming(const uint8_t* data, uint16_t len);
void setBitrate(uint32_t bps);
void setLog(bool on);
const char* identityHex();
uint32_t pathCount();

}  // namespace rns_stack
}  // namespace arb
