// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)

#include "Companion.h"

#if ARB_WITH_COMPANION

#include "mcc/CompanionMesh.h"
#include "McRadioPort.h"
#include "ArbPlatform.h"

#if defined(ESP32)
  #include <helpers/esp32/SerialBLEInterface.h>
#elif defined(NRF52_PLATFORM)
  #include <helpers/nrf52/SerialBLEInterface.h>
#endif

// main.cpp's, shared with the repeater (which does not run in this mode).
extern arb::McRadioPort mc_port;
extern StdRNG fast_rng;
extern SimpleMeshTables tables;

namespace arb {

// Built only in the companion mode: its contacts, channels and message
// queue take tens of kilobytes the other modes keep for Reticulum.
static CompanionStore* store = nullptr;
static CompanionMesh* mesh_ = nullptr;
static SerialBLEInterface* bt = nullptr;
static char ble_name[48];
static char name_buf[32];

bool companionBegin(bool has_display) {
  store = new CompanionStore(ARB_FS, rtc_clock);
  mesh_ = new CompanionMesh(mc_port, fast_rng, rtc_clock, tables, *store, nullptr);
  bt = new SerialBLEInterface();
  if (!store || !mesh_ || !bt) return false;
  store->begin();
  // Prefs, identity (the one the repeater had: same file), contacts,
  // channels; the channel and power go to the arbiter through the port.
  mesh_->begin(has_display);
  // The build's default name is the same on every board: until the app
  // names it, the node is told apart by its key, as the RNode is.
  char* node_name = mesh_->getNodePrefs()->node_name;
  if (strcmp(node_name, ADVERT_NAME) == 0) {
    snprintf(node_name, sizeof(mesh_->getNodePrefs()->node_name), "%s %02X%02X", ADVERT_NAME, mesh_->self_id.pub_key[0], mesh_->self_id.pub_key[1]);
  }
  // The name is an IN/OUT argument ("@@MAC" is replaced): a copy.
  strncpy(name_buf, mesh_->getNodePrefs()->node_name, sizeof(name_buf) - 1);
  bt->begin(BLE_NAME_PREFIX, name_buf, mesh_->getBLEPin());
  snprintf(ble_name, sizeof(ble_name), "%s%s", BLE_NAME_PREFIX, name_buf);
  mesh_->startInterface(*bt);   // enables it: advertising starts
  return true;
}

void companionSensorsReady() {
#if ENV_INCLUDE_GPS == 1
  if (mesh_) mesh_->applyGpsPrefs();
#endif
}

void companionLoop() {
  if (!mesh_) return;
  mesh_->loop();
  bt->loop();
}

bool companionRunning() { return mesh_ != nullptr; }
const char* companionNodeName() { return mesh_ ? mesh_->getNodeName() : ""; }
const char* companionBleName() { return mesh_ ? ble_name : ""; }
uint32_t companionBlePin() { return mesh_ ? mesh_->getBLEPin() : 0; }
bool companionConnected() { return bt && bt->isConnected(); }

}  // namespace arb

#else  // no Bluetooth on this board: no companion

namespace arb {
bool companionBegin(bool) { return false; }
void companionSensorsReady() {}
void companionLoop() {}
bool companionRunning() { return false; }
const char* companionNodeName() { return ""; }
const char* companionBleName() { return ""; }
uint32_t companionBlePin() { return 0; }
bool companionConnected() { return false; }
}  // namespace arb

#endif
