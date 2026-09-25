// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// McRedirect.h — included by app/mc/MyMesh.cpp (MeshCore's repeater) right
// after its own header, and nowhere else.
//
// The repeater calls the board's `radio_driver` directly for its channel,
// power and statistics. In Arborisis Mesh the chip belongs to the arbiter,
// so those calls go to the MeshCore port instead, which has the same
// methods. The board's file system is LittleFS on ESP32 (see
// ArbPlatform.h), so the repeater's `format` command formats that one.

#pragma once

#include "McRadioPort.h"

extern arb::McRadioPort mc_port;
#define radio_driver mc_port

#if defined(ESP32)
  #include <LittleFS.h>
  #define SPIFFS LittleFS
#endif
