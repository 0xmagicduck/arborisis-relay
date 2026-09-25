// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// ArbProfile.h — the network a fresh device joins, for both protocols.
//
// Reticulum: the Arborisis Belgium channel, the one the Arborisis relays
// (Heltec V3/V4, ../Arborisis.h) and the Pocket use — 869.525 MHz, 125 kHz,
// SF8, CR 4/5. A device that differs on any one of the first three never
// hears the network.
//
// MeshCore: MeshCore's own default for Europe (LORA_FREQ / LORA_BW / LORA_SF
// in platformio.ini: 869.618 MHz, 62.5 kHz, SF8), which its CLI and app
// change as on any MeshCore repeater. It is not repeated here.
//
// Both channels sit in the 869.4–869.65 MHz sub-band (ETSI EN 300 220):
// 500 mW e.r.p., 10 % duty cycle. The device-wide budget below enforces the
// 10 % over the two protocols together — a MeshCore repeater and a
// Reticulum transport sharing one transmitter share one allowance.
//
// Every value can be overridden with a -D in platformio.local.ini, and the
// console (`arb …`) changes them on a running device.

#pragma once

#ifndef ARB_DEFAULT_MODE
  #define ARB_DEFAULT_MODE     3          // MODE_DUAL
#endif

#ifndef ARB_RNS_FREQ_HZ
  #define ARB_RNS_FREQ_HZ      869525000UL
#endif
#ifndef ARB_RNS_BW_HZ
  #define ARB_RNS_BW_HZ        125000UL
#endif
#ifndef ARB_RNS_SF
  #define ARB_RNS_SF           8
#endif
#ifndef ARB_RNS_CR
  #define ARB_RNS_CR           5
#endif
#ifndef ARB_RNS_TXP_DBM
  #define ARB_RNS_TXP_DBM      22
#endif

#ifndef ARB_DUTY_CYCLE_X100
  #define ARB_DUTY_CYCLE_X100  1000       // 10 %
#endif

#ifndef ARB_NODE_NAME
  #define ARB_NODE_NAME        "Arborisis"
#endif

// RNode over Bluetooth LE (Sideband), on the boards that have BLE. On by
// default: that is how a phone uses a Reticulum radio. Pairing needs the
// passkey shown on the display and the console.
#ifndef ARB_BLE_DEFAULT
  #define ARB_BLE_DEFAULT      1
#endif
