// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// RadioArbiter.h — one LoRa transceiver, two networks.
//
// The arbiter owns the radio. MeshCore talks to it through McRadioPort (a
// mesh::Radio), Reticulum through rnsEnqueue() and the RxHandler callback.
// It keeps each protocol's channel, decides where the receiver sits
// (ChannelPlan), peeks at the other channel by CAD, transmits each
// protocol's frames on that protocol's channel with that protocol's
// preamble, and keeps the device inside one airtime budget.
//
// The hardware underneath is whatever the MeshCore board file built:
// `radio_driver` (a RadioLibWrapper for an SX1262, SX1268, SX1276, LR1110,
// LR2021 or STM32WL) and `board`. The arbiter reprograms the channel with
// the wrapper's own setParams() — so every chip quirk MeshCore handles is
// handled — and drives transmit, receive and CAD itself, through RadioLib's
// PhysicalLayer, from one interrupt flag.

#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>

#include "Airtime.h"
#include "ChannelPlan.h"
#include "LoRaMath.h"
#include "RNodeFraming.h"

namespace arb {

class RxHandler {
public:
  virtual ~RxHandler() {}
  // A raw LoRa frame heard on the Reticulum channel (RNode framing).
  virtual void onRnsFrame(const uint8_t* frame, uint16_t len, float rssi, float snr) = 0;
  // The Reticulum packet at the head of the queue has been sent.
  virtual void onRnsTransmitted() {}
};

struct ArbiterStats {
  uint32_t rx[2] = {0, 0};
  uint32_t tx[2] = {0, 0};
  uint32_t rx_errors = 0;
  uint32_t peeks = 0;
  uint32_t peek_hits = 0;
  uint32_t dwell_misses = 0;
  uint32_t duty_refusals = 0;
  uint32_t tx_timeouts = 0;
  uint32_t queue_drops = 0;
};

class RadioArbiter {
public:
  static const uint8_t  RNS_QUEUE_LEN = 8;
  static const uint8_t  MC_QUEUE_LEN  = 4;

  bool begin(RadioLibWrapper& driver, mesh::MainBoard& board);

  // Channels and roles. Changing either replans the receiver.
  void setChannel(Proto p, const LoRaChannel& ch);
  const LoRaChannel& channel(Proto p) const { return _ch[p]; }
  void setEnabled(bool mc, bool rns);
  bool enabled(Proto p) const { return _on[p]; }
  const ChannelPlan& plan() const { return _plan; }

  // Device-wide long-term transmit share (0 … 1, 0 = none), and the
  // Reticulum-only one an RNode host sets with CMD_LT_ALOCK.
  void setDutyCycle(float share) { _duty = share; }
  void setRnsAirtimeLock(float share) { _rns_lock = share; }
  void setRxHandler(RxHandler* h) { _rx_handler = h; }

  void loop();

  // ── MeshCore side (McRadioPort) ──────────────────────────────────────────
  bool  mcChannelBusy();
  bool  mcStartTx(const uint8_t* raw, int len);
  bool  mcTxComplete();
  void  mcTxFinished();
  bool  mcTransmitting() const { return _state == S_TX && _tx_proto == PROTO_MC; }
  int   mcRecv(uint8_t* out, int sz, float& rssi, float& snr);
  void  resetAgc();

  // ── Reticulum side ───────────────────────────────────────────────────────
  bool  rnsEnqueue(const uint8_t* pkt, uint16_t len);
  bool  rnsQueueHasRoom() const { return _rns_count < RNS_QUEUE_LEN; }
  uint8_t rnsQueued() const { return _rns_count; }
  // Pending first half of a split frame (for the shared-channel sorter).
  void  noteRnsSplitPending(bool pending, uint8_t seq) { _rns_split_pending = pending; _rns_split_seq = seq; }

  // ── Status ───────────────────────────────────────────────────────────────
  const ArbiterStats& stats() const { return _stats; }
  int16_t noiseFloor() const { return _noise_floor; }
  int16_t currentRssi();
  float   airtimeShort() { return _air_all.shortTerm(millis()); }
  float   airtimeLong() { return _air_all.longTerm(millis()); }
  float   airtimeLong(Proto p) { return _air[p].longTerm(millis()); }
  uint32_t airtimeTotalMs(Proto p) const { return _air[p].totalMs(); }
  const char* stateName() const;
  uint8_t randomByte();
  RadioLibWrapper* driver() { return _drv; }

private:
  enum State : uint8_t { S_OFF, S_IDLE, S_LISTEN, S_CAD, S_DWELL, S_TX };
  enum CadPurpose : uint8_t { CAD_PEEK, CAD_LBT_RNS, CAD_LBT_MC };

  void replan();
  void program(Proto p, uint16_t preamble);
  void startListen();
  void startCad(Proto p, CadPurpose why);
  void startTx(Proto p, const uint8_t* buf, uint16_t len);
  void tryRnsTx(uint32_t now);
  void onTxDone(bool ok);
  void onRxDone();
  void onCadDone();
  void enterDwell(Proto p, uint32_t now);
  void dispatch(Proto p, const uint8_t* buf, uint16_t len, float rssi, float snr);
  bool receivingPacket();
  bool dutyAllows(uint32_t ms);
  void sampleNoiseFloor(uint32_t now);

  static void isr();
  static RadioArbiter* _self;
  volatile bool _irq = false;

  RadioLibWrapper* _drv = nullptr;
  PhysicalLayer* _phy = nullptr;
  mesh::MainBoard* _board = nullptr;

  LoRaChannel _ch[2];
  bool _on[2] = {false, false};
  ChannelPlan _plan;
  float _duty = 0.0f, _rns_lock = 0.0f;
  RxHandler* _rx_handler = nullptr;

  State _state = S_OFF;
  // What the radio is programmed with right now.
  bool _prog_valid = false;
  LoRaChannel _prog;
  int8_t _prog_txp = -128;

  // Transmit.
  Proto _tx_proto = PROTO_NONE;
  uint16_t _tx_len = 0;
  uint32_t _tx_started = 0, _tx_timeout = 0;
  RNodeTxFrames _rns_frames;
  uint8_t _rns_frame_idx = 0;
  bool _mc_tx_done = false, _mc_tx_ok = false;

  // CAD / dwell.
  Proto _cad_proto = PROTO_NONE;
  CadPurpose _cad_why = CAD_PEEK;
  uint32_t _cad_started = 0, _cad_timeout = 0;
  Proto _dwell_proto = PROTO_NONE;
  uint32_t _dwell_deadline = 0, _dwell_hard_deadline = 0;
  uint32_t _next_peek = 0;

  // Reticulum transmit queue: whole packets, framed at transmission.
  struct RnsSlot { uint16_t len; uint8_t data[RNODE_MTU]; };
  RnsSlot _rns_q[RNS_QUEUE_LEN];
  uint8_t _rns_head = 0, _rns_count = 0;
  uint32_t _rns_next_try = 0;
  bool _rns_split_pending = false;
  uint8_t _rns_split_seq = 0;

  // MeshCore receive queue.
  struct McSlot { uint16_t len; float rssi, snr; uint8_t data[RNODE_FRAME_MAX]; };
  McSlot _mc_q[MC_QUEUE_LEN];
  uint8_t _mc_head = 0, _mc_count = 0;

  // Noise floor on the listen channel.
  int16_t _noise_floor = -120;
  int32_t _nf_sum = 0;
  uint8_t _nf_n = 0;
  uint32_t _nf_next = 0;

  AirtimeMeter _air_all;
  AirtimeMeter _air[2];
  ArbiterStats _stats;
};

}  // namespace arb
