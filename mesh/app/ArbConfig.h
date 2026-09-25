// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// ArbConfig.h — the settings Arborisis Mesh keeps on top of MeshCore's own
// (MeshCore stores its channel, name, passwords and repeater preferences in
// its prefs file, and its CLI changes them as on any MeshCore repeater).
//
// The record is a fixed-layout struct with a magic, a version and a CRC, so
// a record written by another build is recognised and replaced by the
// defaults rather than misread. Pure C++: the host tests check the
// defaults, the validation and the CRC.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "LoRaMath.h"
#include "ArbProfile.h"

namespace arb {

enum Mode : uint8_t {
  MODE_RNODE    = 0,   // a plain RNode for a Reticulum host; nothing on the device
  MODE_RNS      = 1,   // Reticulum on the device (transport), RNode host protocol too
  MODE_MESHCORE = 2,   // MeshCore repeater only (the RNode host can still take the radio)
  MODE_DUAL     = 3,   // MeshCore repeater and Reticulum on the device, sharing the radio
  MODE_COMPANION = 4,  // MeshCore companion for the MeshCore app, over Bluetooth LE (RNode on USB)
};
static const uint8_t MODE_LAST = MODE_COMPANION;

inline const char* modeName(uint8_t m) {
  switch (m) {
    case MODE_RNODE:    return "rnode";
    case MODE_RNS:      return "rns";
    case MODE_MESHCORE: return "meshcore";
    case MODE_DUAL:     return "dual";
    case MODE_COMPANION: return "companion";
    default:            return "?";
  }
}

inline bool modeFromName(const char* s, uint8_t& out) {
  for (uint8_t m = 0; m <= MODE_LAST; m++) {
    if (strcmp(s, modeName(m)) == 0) { out = m; return true; }
  }
  return false;
}

// MeshCore runs in these modes: the repeater, or the companion.
inline bool modeHasMeshCore(uint8_t m) { return m == MODE_MESHCORE || m == MODE_DUAL || m == MODE_COMPANION; }
inline bool modeHasRns(uint8_t m)      { return m == MODE_RNS || m == MODE_DUAL; }

static const uint32_t ARB_CFG_MAGIC   = 0x31425241UL;   // "ARB1"
static const uint16_t ARB_CFG_VERSION = 1;

struct ArbConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t size;

  uint8_t  mode;             // Mode
  uint8_t  rns_transport;    // 1: forward for others (transport node); 0: endpoint
  uint8_t  log;              // 1: Reticulum / arbiter log lines on the console
  uint8_t  display_timeout;  // seconds the display stays on; 0 = always on

  // The Reticulum LoRa channel (the RNode host may change it while attached).
  uint32_t rns_freq_hz;
  uint32_t rns_bw_hz;
  uint8_t  rns_sf;
  uint8_t  rns_cr;
  int8_t   rns_txp_dbm;
  uint8_t  _pad0;

  // Whole-device transmit budget, long-term share in % × 100 (1000 = 10 %),
  // over both protocols. 0 = no limit. EU 869.4–869.65 MHz: 10 %.
  uint16_t duty_cycle_x100;
  // Reticulum path table on the device (entries); 0 = firmware default.
  uint16_t rns_path_table;

  char     name[32];         // shown on the display
  uint8_t  ble;              // 1: RNode over Bluetooth LE (boards with BLE)
  uint8_t  _pad1[3];
  uint32_t ble_pin;          // six-digit pairing passkey
  uint8_t  reserved[24];

  uint32_t crc;
};

inline uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFUL;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320UL & (0UL - (c & 1)));
  }
  return ~c;
}

inline uint32_t configCrc(const ArbConfig& c) {
  return crc32(reinterpret_cast<const uint8_t*>(&c), offsetof(ArbConfig, crc));
}

inline void configDefaults(ArbConfig& c, int8_t board_max_txp) {
  memset(&c, 0, sizeof(c));
  c.magic = ARB_CFG_MAGIC;
  c.version = ARB_CFG_VERSION;
  c.size = sizeof(ArbConfig);
  c.mode = ARB_DEFAULT_MODE;
  c.rns_transport = 1;
  c.log = 0;
  c.display_timeout = 60;
  c.rns_freq_hz = ARB_RNS_FREQ_HZ;
  c.rns_bw_hz = ARB_RNS_BW_HZ;
  c.rns_sf = ARB_RNS_SF;
  c.rns_cr = ARB_RNS_CR;
  c.rns_txp_dbm = ARB_RNS_TXP_DBM < board_max_txp ? ARB_RNS_TXP_DBM : board_max_txp;
  c.duty_cycle_x100 = ARB_DUTY_CYCLE_X100;
  c.rns_path_table = 0;
  strncpy(c.name, ARB_NODE_NAME, sizeof(c.name) - 1);
  c.ble = ARB_BLE_DEFAULT;
  c.ble_pin = 0;             // 0: drawn at first boot (main.cpp)
  c.crc = configCrc(c);
}

inline bool configValid(const ArbConfig& c) {
  if (c.magic != ARB_CFG_MAGIC || c.version != ARB_CFG_VERSION || c.size != sizeof(ArbConfig)) return false;
  if (c.crc != configCrc(c)) return false;
  if (c.mode > MODE_LAST) return false;
  LoRaChannel ch;
  ch.freq_hz = c.rns_freq_hz; ch.bw_hz = c.rns_bw_hz; ch.sf = c.rns_sf; ch.cr = c.rns_cr;
  if (!ch.valid()) return false;
  if (c.duty_cycle_x100 > 10000) return false;
  if (c.ble_pin > 999999) return false;
  return c.name[sizeof(c.name) - 1] == 0;
}

inline LoRaChannel rnsChannelOf(const ArbConfig& c) {
  LoRaChannel ch;
  ch.freq_hz = c.rns_freq_hz;
  ch.bw_hz = c.rns_bw_hz;
  ch.sf = c.rns_sf;
  ch.cr = c.rns_cr;
  ch.txp_dbm = c.rns_txp_dbm;
  ch.preamble = rnodePreambleSymbols(ch.bw_hz, ch.sf, ch.cr);
  return ch;
}

}  // namespace arb
