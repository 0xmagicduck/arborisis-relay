// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// ArbStore.h — the Arborisis settings record on the board's file system.

#pragma once

#include "ArbConfig.h"
#include "ArbPlatform.h"

namespace arb {

static const char* const ARB_CFG_PATH = "/arb_cfg";

inline bool loadConfig(ArbConfig& c) {
  if (!ARB_FS.exists(ARB_CFG_PATH)) return false;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f(ARB_FS);
  if (!f.open(ARB_CFG_PATH, FILE_O_READ)) return false;
#else
  File f = ARB_FS.open(ARB_CFG_PATH, "r");
  if (!f) return false;
#endif
  ArbConfig tmp;
  size_t n = f.read((uint8_t*)&tmp, sizeof(tmp));
  f.close();
  if (n != sizeof(tmp) || !configValid(tmp)) return false;
  c = tmp;
  return true;
}

inline bool saveConfig(ArbConfig& c) {
  c.magic = ARB_CFG_MAGIC;
  c.version = ARB_CFG_VERSION;
  c.size = sizeof(ArbConfig);
  c.crc = configCrc(c);
  if (ARB_FS.exists(ARB_CFG_PATH)) ARB_FS.remove(ARB_CFG_PATH);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f(ARB_FS);
  if (!f.open(ARB_CFG_PATH, FILE_O_WRITE)) return false;
#else
  File f = ARB_FS.open(ARB_CFG_PATH, "w");
  if (!f) return false;
#endif
  size_t n = f.write((const uint8_t*)&c, sizeof(c));
  f.close();
  return n == sizeof(c);
}

}  // namespace arb
