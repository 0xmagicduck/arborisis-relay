// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// ArbApp.h — the pieces of the running firmware, for the console and the
// display to look at. Defined in main.cpp.

#pragma once

#include "ArbConfig.h"
#include "BleLink.h"
#include "RadioArbiter.h"
#include "RNodeHost.h"
#include "RnsSide.h"

namespace arb {

struct App {
  ArbConfig cfg;
  bool mc_running = false;       // MeshCore repeater active this boot
  const char* mc_name = "";      // MeshCore node name (its prefs)
  const char* mc_pubkey_hex = "";
  uint32_t boot_ms = 0;
  bool config_loaded = false;    // false: defaults (first boot, or unreadable record)
  char ble_name[16] = {0};       // "RNode 1A2B" when BLE runs
};

extern App app;
extern RadioArbiter arbiter;
extern RnsSide rns_side;
extern RNodeHost rnode_host;     // on USB
extern RNodeHost ble_host;       // on Bluetooth LE
extern BleLink ble;

// Implemented in main.cpp.
void appSaveConfig();
void appApplyLive();                 // channel / duty / log changes, no reboot
void appReboot();
int  appMeshCoreCommand(char* cmd, char* reply);   // 0 if MeshCore is not built in
uint32_t appNeighbourCount();

}  // namespace arb
