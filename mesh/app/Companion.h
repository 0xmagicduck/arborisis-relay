// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// Companion.h — the MeshCore companion, for the MeshCore app (mode
// `companion`).
//
// The MeshCore app does not talk to a repeater: over Bluetooth LE it looks
// for a *companion* named "MeshCore-<name>" and speaks the companion frame
// protocol to it (contacts, channels, messages; administration of remote
// repeaters goes through it). In the `companion` mode Arborisis Mesh runs
// MeshCore's own companion (app/mcc/, imported by tools/import_companion.py)
// instead of the repeater, on the arbiter's MeshCore port, and gives it the
// board's Bluetooth: the RNode over BLE is off in that mode, the RNode over
// USB stays.
//
// A separate unit from main.cpp: the companion's and the repeater's headers
// define the same macros with different values.

#pragma once

#include <Arduino.h>

#ifndef ARB_WITH_BLE
  #define ARB_WITH_BLE 0
#endif

// The companion is built where there is Bluetooth to reach it by.
#define ARB_WITH_COMPANION ARB_WITH_BLE

namespace arb {

// Loads the companion's prefs, contacts and channels, hands its channel to
// the arbiter and starts Bluetooth. False if the companion could not start.
bool companionBegin(bool has_display);
void companionSensorsReady();      // after sensors.begin(): the GPS as the app left it
void companionLoop();
bool companionRunning();

const char* companionNodeName();   // the node's MeshCore name (the app sets it)
const char* companionBleName();    // "MeshCore-<name>", "" when not running
uint32_t companionBlePin();        // the passkey the phone asks for
bool companionConnected();         // the app is connected

}  // namespace arb
