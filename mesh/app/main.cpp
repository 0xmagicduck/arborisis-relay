// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// Arborisis Mesh — MeshCore and Reticulum on one LoRa radio.
//
//   board, radio_driver, rtc_clock, sensors, display   the MeshCore board file
//                                                       (variants/<board>/)
//   arbiter      RadioArbiter: the one transceiver, both channels
//   the_mesh     MeshCore's repeater (app/mc), on the arbiter's MeshCore port
//   rns_side     Reticulum: RNode host backend + microReticulum transport
//   rnode_host   the RNode protocol on the USB port (rnsd, Sideband, MeshChat)
//   console      the USB port: KISS frames, `arb` lines, MeshCore CLI lines
//   ui           the display pages
//
// See docs/ARCHITECTURE.md.

#include <Arduino.h>
#include <Mesh.h>

#include "mc/MyMesh.h"

#include "ArbApp.h"
#include "ArbPlatform.h"
#include "ArbStore.h"
#include "ArbUI.h"
#include "BleLink.h"
#include "Console.h"
#include "McRadioPort.h"

namespace arb {
App app;
RadioArbiter arbiter;
RnsSide rns_side(arbiter, board);
SerialSink serial_sink;
RNodeHost rnode_host(rns_side.port(0), serial_sink, rnodeIdentity());
BleLink ble;
RNodeHost ble_host(rns_side.port(1), ble, rnodeIdentity());
}  // namespace arb

using namespace arb;

McRadioPort mc_port(arbiter);

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(board, mc_port, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

static bool radio_ok = false;
static char mc_pubkey_hex[2 * PUB_KEY_SIZE + 1];
static kiss::Decoder<600> ble_kiss;
static bool ble_was_connected = false;

// ── Hooks for the console and the display ──────────────────────────────────

namespace arb {

void appSaveConfig() { saveConfig(app.cfg); }

void appApplyLive() {
  arbiter.setDutyCycle(app.cfg.duty_cycle_x100 / 10000.0f);
  rnode_host.setDefaults(rnsChannelOf(app.cfg));
  ble_host.setDefaults(rnsChannelOf(app.cfg));
  rns_side.applyRole();
}

void appReboot() { board.reboot(); }

int appMeshCoreCommand(char* cmd, char* reply) {
  the_mesh.handleCommand(0, cmd, reply);   // MeshCore's own repeater CLI
  return 1;
}

uint32_t appNeighbourCount() { return 0; }

}  // namespace arb

// ── Bluetooth LE ───────────────────────────────────────────────────────────

static void startBle() {
#if ARB_WITH_BLE
  if (!app.cfg.ble) return;
  if (app.cfg.ble_pin == 0) {   // first boot: draw a passkey and keep it
    app.cfg.ble_pin = 100000 + (uint32_t)::random(0, 900000);
    saveConfig(app.cfg);
  }
  // "RNode XXXX": the name Sideband and RNS look for.
  snprintf(app.ble_name, sizeof(app.ble_name), "RNode %02X%02X", the_mesh.self_id.pub_key[0],
           the_mesh.self_id.pub_key[1]);
  if (!ble.begin(app.ble_name, app.cfg.ble_pin)) app.ble_name[0] = 0;
#endif
}

static void bleLoop(uint32_t now) {
#if ARB_WITH_BLE
  if (!ble.running()) return;
  bool c = ble.connected();
  if (ble_was_connected && !c && ble_host.attached()) ble_host.onFrame(rnode::CMD_LEAVE, nullptr, 0);
  if (!c) ble_kiss.reset();
  ble_was_connected = c;
  int budget = 512;
  while (budget-- > 0 && ble.available() > 0) {
    uint8_t t;
    if (ble_kiss.feed((uint8_t)ble.read(), t) == kiss::Decoder<600>::KISS_FRAME)
      ble_host.onFrame(ble_kiss.command(), ble_kiss.payload(), ble_kiss.payloadLen());
  }
  ble_host.tick(now);
#else
  (void)now;
#endif
}

// ── Boot ───────────────────────────────────────────────────────────────────

static void loadMeshCoreIdentity() {
#if defined(RP2040_PLATFORM)
  IdentityStore store(ARB_FS, ARB_ID_DIR);
  store.begin();
#else
  IdentityStore store(ARB_FS, ARB_ID_DIR);
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();
    int count = 0;
    // 0x00 and 0xFF are reserved first bytes of a MeshCore node hash.
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity();
      count++;
    }
    store.save("_main", the_mesh.self_id);
  }
  for (int i = 0; i < PUB_KEY_SIZE; i++) sprintf(&mc_pubkey_hex[i * 2], "%02x", the_mesh.self_id.pub_key[i]);
  app.mc_pubkey_hex = mc_pubkey_hex;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  app.boot_ms = millis();

  board.begin();
  app.board_name = board.getManufacturerName();

#ifdef DISPLAY_CLASS
  if (display.begin()) ui.splash("starting...");
#endif

  if (!radio_init()) {
    Serial.println("[arb] radio init failed: check the board and its antenna");
#ifdef DISPLAY_CLASS
    ui.splash("radio init failed");
#endif
    fsBegin();
    if (!loadConfig(app.cfg)) configDefaults(app.cfg, LORA_TX_POWER);
    console.begin();
    return;
  }
  radio_ok = true;

  // Seeds, from the radio's noise, before the arbiter takes the chip.
  const uint32_t seed = radio_driver.getRngSeed();
  fast_rng.begin(seed);
  randomSeed(seed ^ micros());

  if (!fsBegin()) {
    Serial.println("[arb] file system mount failed, formatting");
    fsFormat();
    fsBegin();
  }

  app.config_loaded = loadConfig(app.cfg);
  if (!app.config_loaded) {
    configDefaults(app.cfg, LORA_TX_POWER);
    saveConfig(app.cfg);
  }
#if !ARB_WITH_RNS
  // No room for Reticulum on the device: dual becomes MeshCore, rns becomes
  // rnode (the board stays a Reticulum modem for a host over USB).
  if (app.cfg.mode == MODE_DUAL) app.cfg.mode = MODE_MESHCORE;
  if (app.cfg.mode == MODE_RNS) app.cfg.mode = MODE_RNODE;
#endif

  // A new MeshCore identity draws on the radio's noise: before the arbiter
  // takes the chip.
  loadMeshCoreIdentity();

  arbiter.begin(radio_driver, board);
  arbiter.setDutyCycle(app.cfg.duty_cycle_x100 / 10000.0f);
  app.mc_running = modeHasMeshCore(app.cfg.mode);
  arbiter.setEnabled(app.mc_running, false);

  // MeshCore: always loaded — its prefs are the ones its CLI edits, in
  // every mode — and run only in the modes that have it. begin() hands its
  // channel and power to the arbiter through the port.
  the_mesh.begin(&ARB_FS);
  app.mc_name = the_mesh.getNodeName();
  sensors.begin();

  // Reticulum.
  rns_side.setHost(0, &rnode_host);
  rns_side.setHost(1, &ble_host);
  rns_side.begin(&app.cfg);
  rnode_host.setDefaults(rnsChannelOf(app.cfg));
  ble_host.setDefaults(rnsChannelOf(app.cfg));
  if (modeHasRns(app.cfg.mode)) rns_side.startStack();
  arbiter.setRxHandler(&rns_side);

  console.begin();
  rnode_host.announceReset();
  startBle();
  ui.begin();

#if ENABLE_ADVERT_ON_BOOT == 1
  if (app.mc_running) the_mesh.sendSelfAdvertisement(16000, false);
#endif
  board.onBootComplete();

  char status[800];
  Console::statusText(status, sizeof(status));
  Serial.print("\r\n[arb] ");
  Serial.println(status);
}

void loop() {
  if (!radio_ok) {
    console.loop();
    return;
  }
  arbiter.loop();
  console.loop();
  if (app.mc_running) the_mesh.loop();
  sensors.loop();
  rtc_clock.tick();
  rns_side.loop();
  arbiter.loop();
  bleLoop(millis());
  ui.loop();
}
