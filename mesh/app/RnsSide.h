// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// RnsSide.h — everything Reticulum on the device.
//
// Up to three users of the Reticulum channel meet here:
//
//   • a Reticulum host on USB and one on Bluetooth LE (rnsd, Sideband,
//     MeshChat…) speaking the RNode protocol — each RNodeHost is its end of
//     the line, and a Port of RnsSide its backend;
//   • the Reticulum stack on the board itself (microReticulum, RnsStack), a
//     transport node with the LoRa channel as its interface, on the boards
//     with the memory for it (ARB_WITH_RNS).
//
// A frame heard on the Reticulum channel is reassembled once (RNodeFraming)
// and given to all of them. While a host has its radio on, the channel it
// set is the Reticulum channel (the last host to set one wins); when no
// host has, the configured channel returns.

#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include "ArbConfig.h"
#include "ArbPlatform.h"
#include "RNodeFraming.h"
#include "RNodeHost.h"
#include "RadioArbiter.h"

namespace arb {

class RnsSide : public RxHandler {
public:
  static const uint8_t HOSTS = 2;   // 0: USB, 1: BLE

  RnsSide(RadioArbiter& arb, mesh::MainBoard& board)
    : _arb(arb), _board(board), _ports{ Port(*this, 0), Port(*this, 1) } {}

  // The RNode backend for host slot `i`, to construct its RNodeHost with.
  RNodeBackend& port(uint8_t i) { return _ports[i]; }
  void setHost(uint8_t i, RNodeHost* host) { if (i < HOSTS) _hosts[i] = host; }

  void begin(ArbConfig* cfg) { _cfg = cfg; applyRole(); }
  bool startStack();
  void loop();

  bool stackRunning() const { return _stack_running; }
  bool hostActive() const { return _host_on[0] || _host_on[1]; }
  bool hostActive(uint8_t i) const { return i < HOSTS && _host_on[i]; }
  const char* identityHex() const { return _identity_hex; }
  uint32_t pathCount() const;
  uint32_t packetsIn() const { return _pk_in; }
  uint32_t packetsOut() const { return _pk_out; }

  // Re-read the configuration and the hosts' state into the arbiter.
  void applyRole();

  // RxHandler
  void onRnsFrame(const uint8_t* frame, uint16_t len, float rssi, float snr) override;
  void onRnsTransmitted() override;

  // For the stack's LoRa interface.
  bool stackSend(const uint8_t* data, uint16_t len);

private:
  class Port : public RNodeBackend {
  public:
    Port(RnsSide& s, uint8_t i) : _s(s), _i(i) {}
    int8_t hostApplyRadio(const LoRaChannel& ch, bool on) override { return _s.hostApplyRadio(_i, ch, on); }
    bool hostSubmit(const uint8_t* d, uint16_t n) override { return _s._arb.rnsEnqueue(d, n); }
    bool hostQueueHasRoom() override { return _s._arb.rnsQueueHasRoom(); }
    void hostAttached() override { _s.hostAttached(_i); }
    void hostLeft() override { _s.hostLeft(_i); }
    void hostReset() override { _s._board.reboot(); }
    uint8_t randomByte() override { return _s._arb.randomByte(); }
    void channelReport(float& ats, float& atl, float& cls, float& cll, int16_t& crs, int16_t& nfl) override {
      _s.channelReport(ats, atl, cls, cll, crs, nfl);
    }
    void battery(uint8_t& state, uint8_t& percent) override { _s.battery(state, percent); }
  private:
    RnsSide& _s;
    uint8_t _i;
  };

  int8_t hostApplyRadio(uint8_t i, const LoRaChannel& ch, bool on);
  void hostAttached(uint8_t i);
  void hostLeft(uint8_t i);
  void channelReport(float& ats, float& atl, float& cls, float& cll, int16_t& crs, int16_t& nfl);
  void battery(uint8_t& state, uint8_t& percent);

  RadioArbiter& _arb;
  mesh::MainBoard& _board;
  Port _ports[HOSTS];
  RNodeHost* _hosts[HOSTS] = { nullptr, nullptr };
  ArbConfig* _cfg = nullptr;
  RNodeReassembler _reasm;
  bool _stack_running = false;
  char _identity_hex[33] = {0};
  uint32_t _pk_in = 0, _pk_out = 0;
  bool _host_on[HOSTS] = { false, false };
  LoRaChannel _host_ch[HOSTS];
  int8_t _host_owner = -1;          // the host whose channel is in use
};

}  // namespace arb
