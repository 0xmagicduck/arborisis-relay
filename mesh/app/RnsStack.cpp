// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// No MeshCore header may be included here (see RnsStack.h).

#include "RnsStack.h"
#include "ArbPlatform.h"

#if ARB_WITH_RNS

#include <Bytes.h>
#include <Destination.h>
#include <Interface.h>
#include <Log.h>
#include <Reticulum.h>
#include <Transport.h>
#include <Utilities/OS.h>

#include "RNodeFraming.h"
#include "RnsFileSystem.h"

namespace arb {
namespace rns_stack {

namespace {

// Reticulum's view of the LoRa channel: packets out go to the arbiter's
// queue (through `send`), packets in come from incoming().
class ArbLoRaInterface : public RNS::InterfaceImpl {
public:
  explicit ArbLoRaInterface(SendFn send) : RNS::InterfaceImpl("LoRaInterface"), _send(send) {
    _IN = true;
    _OUT = true;
    _HW_MTU = RNODE_MTU;
  }
  void setBitrate(uint32_t bps) { _bitrate = bps; }

protected:
  void send_outgoing(const RNS::Bytes& data) override {
    if (_send && _send(data.data(), (uint16_t)data.size())) InterfaceImpl::handle_outgoing(data);
  }

private:
  SendFn _send;
};

RNS::Reticulum g_reticulum(RNS::Type::NONE);
RNS::Interface g_lora(RNS::Type::NONE);
RNS::FileSystem g_fs(RNS::Type::NONE);
ArbLoRaInterface* g_lora_impl = nullptr;
bool g_running = false;
bool g_log = false;
char g_identity[33] = {0};

void onLog(const char* msg, RNS::LogLevel level) {
  if (!g_log) return;
  Serial.print("[rns ");
  Serial.print(RNS::getLevelName(level));
  Serial.print("] ");
  Serial.println(msg);
}

uint16_t defaultPathTable() {
#if defined(ESP32)
  return ESP.getPsramSize() > 0 ? 100 : 48;
#elif defined(NRF52_PLATFORM)
  return 16;
#else
  return 32;
#endif
}

}  // namespace

bool start(const Params& p, SendFn send) {
  g_log = p.log;
  try {
    g_fs = new RnsFileSystem();
    g_fs.init();
    RNS::Utilities::OS::register_filesystem(g_fs);

    RNS::setLogCallback(&onLog);
    RNS::loglevel(p.log ? RNS::LOG_NOTICE : RNS::LOG_ERROR);

    g_lora_impl = new ArbLoRaInterface(send);
    g_lora = g_lora_impl;
    g_lora.mode(RNS::Type::Interface::MODE_FULL);
    g_lora_impl->setBitrate(p.bitrate);
    RNS::Transport::register_interface(g_lora);

    const uint16_t paths = p.path_table ? p.path_table : defaultPathTable();
    RNS::Transport::path_table_maxsize(paths);
    RNS::Transport::path_table_maxpersist(paths / 2);

    g_reticulum = RNS::Reticulum();
    g_reticulum.transport_enabled(p.transport);
    g_reticulum.probe_destination_enabled(true);
    g_reticulum.start();

    std::string h = RNS::Transport::identity().hash().toHex();
    strncpy(g_identity, h.c_str(), sizeof(g_identity) - 1);
    g_running = true;
  } catch (std::exception& e) {
    Serial.print("[rns] start failed: ");
    Serial.println(e.what());
    g_running = false;
  }
  return g_running;
}

bool running() { return g_running; }

void loop() {
  if (!g_running) return;
  try {
    g_reticulum.loop();
  } catch (std::exception& e) {
    if (g_log) { Serial.print("[rns] loop: "); Serial.println(e.what()); }
  }
}

void incoming(const uint8_t* data, uint16_t len) {
  if (!g_running || !g_lora) return;
  try {
    RNS::Bytes b(data, len);
    g_lora.handle_incoming(b);
  } catch (std::exception& e) {
    if (g_log) { Serial.print("[rns] inbound: "); Serial.println(e.what()); }
  }
}

void setBitrate(uint32_t bps) { if (g_lora_impl) g_lora_impl->setBitrate(bps); }
void setLog(bool on) {
  g_log = on;
  if (g_running) RNS::loglevel(on ? RNS::LOG_NOTICE : RNS::LOG_ERROR);
}
const char* identityHex() { return g_identity; }
uint32_t pathCount() { return g_running ? RNS::Transport::get_destination_table().size() : 0; }

}  // namespace rns_stack
}  // namespace arb

#else  // !ARB_WITH_RNS: the RNode host protocol still works, nothing runs here

namespace arb {
namespace rns_stack {
bool start(const Params&, SendFn) { return false; }
bool running() { return false; }
void loop() {}
void incoming(const uint8_t*, uint16_t) {}
void setBitrate(uint32_t) {}
void setLog(bool) {}
const char* identityHex() { return ""; }
uint32_t pathCount() { return 0; }
}  // namespace rns_stack
}  // namespace arb

#endif
