// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)

#include "RnsSide.h"
#include "RnsStack.h"

namespace arb {

namespace {
RnsSide* g_side = nullptr;
bool stackSendThunk(const uint8_t* data, uint16_t len) { return g_side && g_side->stackSend(data, len); }
}  // namespace

// ── Roles ──────────────────────────────────────────────────────────────────

void RnsSide::applyRole() {
  if (!_cfg) return;
  const bool stack = ARB_WITH_RNS && modeHasRns(_cfg->mode);
  const bool host = _host_owner >= 0 && _host_on[_host_owner];
  LoRaChannel ch = host ? _host_ch[_host_owner] : rnsChannelOf(*_cfg);
  ch.preamble = rnodePreambleSymbols(ch.bw_hz, ch.sf, ch.cr);
  _arb.setChannel(PROTO_RNS, ch);
  _arb.setEnabled(_arb.enabled(PROTO_MC), hostActive() || stack);
  _arb.setRnsAirtimeLock(host && _hosts[_host_owner] ? _hosts[_host_owner]->ltLock() : 0.0f);
  rns_stack::setBitrate(loraBitrate(ch.bw_hz, ch.sf, ch.cr));
  rns_stack::setLog(_cfg->log);
}

int8_t RnsSide::hostApplyRadio(uint8_t i, const LoRaChannel& ch, bool on) {
  int8_t txp = ch.txp_dbm;
  if (txp > LORA_TX_POWER) txp = LORA_TX_POWER;
  _host_ch[i] = ch;
  _host_ch[i].txp_dbm = txp;
  _host_on[i] = on && ch.valid();
  if (_host_on[i]) {
    _host_owner = i;
  } else if (_host_owner == i) {
    // This host let go: another one that still has its radio on takes over.
    _host_owner = -1;
    for (uint8_t k = 0; k < HOSTS; k++) if (_host_on[k]) _host_owner = k;
  }
  applyRole();
  return txp;
}

void RnsSide::hostAttached(uint8_t i) {
  if (_cfg && _cfg->log) { Serial.print("[arb] RNode host attached on "); Serial.println(i ? "BLE" : "USB"); }
}

void RnsSide::hostLeft(uint8_t i) {
  _host_on[i] = false;
  if (_host_owner == i) {
    _host_owner = -1;
    for (uint8_t k = 0; k < HOSTS; k++) if (_host_on[k]) _host_owner = k;
  }
  applyRole();
  if (_cfg && _cfg->log) { Serial.print("[arb] RNode host left "); Serial.println(i ? "BLE" : "USB"); }
}

bool RnsSide::stackSend(const uint8_t* data, uint16_t len) {
  if (!_arb.rnsEnqueue(data, len)) return false;
  _pk_out++;
  return true;
}

// ── Radio events ───────────────────────────────────────────────────────────

void RnsSide::onRnsFrame(const uint8_t* frame, uint16_t len, float rssi, float snr) {
  RNodeReassembler::Packet pkt;
  bool complete = _reasm.push(frame, len, millis(), pkt);
  _arb.noteRnsSplitPending(_reasm.pending(), _reasm.pendingSeq());
  if (!complete) return;
  _pk_in++;
  for (uint8_t i = 0; i < HOSTS; i++) if (_hosts[i]) _hosts[i]->deliver(pkt.data, pkt.len, rssi, snr);
  if (_stack_running) rns_stack::incoming(pkt.data, pkt.len);
}

void RnsSide::onRnsTransmitted() {
  for (uint8_t i = 0; i < HOSTS; i++) if (_hosts[i]) _hosts[i]->transmitted();
}

// ── Reports for the hosts ──────────────────────────────────────────────────

void RnsSide::channelReport(float& ats, float& atl, float& cls, float& cll, int16_t& crs, int16_t& nfl) {
  ats = _arb.airtimeShort();
  atl = _arb.airtimeLong(PROTO_RNS);
  cls = 0.0f;
  cll = 0.0f;
  crs = _arb.currentRssi();
  nfl = _arb.noiseFloor();
}

void RnsSide::battery(uint8_t& state, uint8_t& percent) {
  uint16_t mv = _board.getBattMilliVolts();
  if (mv < 2500) { state = rnode::BATTERY_UNKNOWN; percent = 0; return; }
  int p = (int)((mv - 3300) * 100L / (4150 - 3300));
  percent = p < 0 ? 0 : (p > 100 ? 100 : p);
  state = _board.isExternalPowered() ? (percent >= 100 ? rnode::BATTERY_CHARGED : rnode::BATTERY_CHARGING)
                                     : rnode::BATTERY_DISCHARGING;
}

// ── The stack ──────────────────────────────────────────────────────────────

bool RnsSide::startStack() {
  if (!ARB_WITH_RNS || !_cfg || !modeHasRns(_cfg->mode)) return false;
  g_side = this;
  rns_stack::Params p;
  p.log = _cfg->log;
  p.transport = _cfg->rns_transport != 0;
  p.path_table = _cfg->rns_path_table;
  const LoRaChannel& ch = _arb.channel(PROTO_RNS);
  p.bitrate = loraBitrate(ch.bw_hz, ch.sf, ch.cr);
  _stack_running = rns_stack::start(p, stackSendThunk);
  if (_stack_running) strncpy(_identity_hex, rns_stack::identityHex(), sizeof(_identity_hex) - 1);
  return _stack_running;
}

void RnsSide::loop() {
  if (_stack_running) rns_stack::loop();
}

uint32_t RnsSide::pathCount() const {
  return _stack_running ? rns_stack::pathCount() : 0;
}

}  // namespace arb
