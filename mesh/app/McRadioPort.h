// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// McRadioPort.h — the radio as MeshCore's repeater sees it.
//
// MeshCore's Dispatcher drives a mesh::Radio: poll for a received frame,
// ask whether the channel is busy, start a send, poll for its end. This
// port answers from the arbiter instead of from the chip, so the repeater
// runs unchanged next to Reticulum. The repeater also calls a handful of
// methods on the board's `radio_driver` directly (channel, power, stats);
// app/mc/MyMesh.cpp is compiled with `radio_driver` redirected here
// (app/mc/McRedirect.h), so those land on the port too — a channel change
// from the MeshCore CLI or app becomes a change of the arbiter's MeshCore
// channel, not a reprogramming of the chip behind Reticulum's back.

#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include "RadioArbiter.h"

namespace arb {

class McRadioPort : public mesh::Radio {
public:
  explicit McRadioPort(RadioArbiter& arb) : _arb(arb) {}

  // ── mesh::Radio ──────────────────────────────────────────────────────────
  void begin() override {}

  int recvRaw(uint8_t* bytes, int sz) override {
    float rssi, snr;
    int len = _arb.mcRecv(bytes, sz, rssi, snr);
    if (len > 0) { _last_rssi = rssi; _last_snr = snr; _n_recv++; }
    return len;
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    return timeOnAirMs(_arb.channel(PROTO_MC), (uint16_t)len_bytes);
  }

  float packetScore(float snr, int packet_len) override {
    // RadioLibWrapper::packetScoreInt, at the channel's own SF.
    static const float thr[] = { -7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f };
    const int sf = _arb.channel(PROTO_MC).sf;
    if (sf < 7 || sf > 12) return 0.0f;
    if (snr < thr[sf - 7]) return 0.0f;
    float rate = (snr - thr[sf - 7]) / 10.0f;
    float penalty = 1.0f - (packet_len / 256.0f);
    float v = rate * penalty;
    return v < 0 ? 0 : (v > 1 ? 1 : v);
  }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    bool ok = _arb.mcStartTx(bytes, len);
    if (!ok) _n_send_fail++;
    return ok;
  }

  bool isSendComplete() override {
    if (_arb.mcTxComplete()) { _n_sent++; return true; }
    return false;
  }

  void onSendFinished() override { _arb.mcTxFinished(); }

  bool isInRecvMode() const override { return !_arb.mcTransmitting(); }

  bool isReceiving() override { return _arb.mcChannelBusy(); }

  int getNoiseFloor() const override { return _arb.noiseFloor(); }
  void triggerNoiseFloorCalibrate(int threshold) override { _threshold = threshold; }
  void setCADEnabled(bool enable) override { _cad = enable; }
  void resetAGC() override { _arb.resetAgc(); }

  float getLastRSSI() const override { return _last_rssi; }
  float getLastSNR() const override { return _last_snr; }

  // ── What MyMesh calls on radio_driver ────────────────────────────────────
  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) {
    LoRaChannel ch = _arb.channel(PROTO_MC);
    ch.freq_hz = (uint32_t)(freq * 1000000.0f + 0.5f);
    ch.bw_hz = snapBandwidth((uint32_t)(bw * 1000.0f + 0.5f));
    ch.sf = sf;
    ch.cr = cr;
    ch.preamble = meshcorePreambleSymbols(sf);
    _arb.setChannel(PROTO_MC, ch);
  }
  void setTxPower(int8_t dbm) {
    if (dbm > LORA_TX_POWER_MAX) dbm = LORA_TX_POWER_MAX;
    LoRaChannel ch = _arb.channel(PROTO_MC);
    ch.txp_dbm = dbm;
    _arb.setChannel(PROTO_MC, ch);
  }
  bool setRxBoostedGainMode(bool en) { return _arb.driver()->setRxBoostedGainMode(en); }
  bool getRxBoostedGainMode() const { return _arb.driver()->getRxBoostedGainMode(); }
  bool configSideDetectors(const uint8_t sfs[], uint8_t num, float bw) {
    return _arb.driver()->configSideDetectors(sfs, num, bw);
  }
  uint32_t getRngSeed() { return _arb.driver()->getRngSeed(); }
  uint32_t getPacketsRecv() const { return _n_recv; }
  uint32_t getPacketsSent() const { return _n_sent; }
  uint32_t getPacketsRecvErrors() const { return _arb.stats().rx_errors; }
  void resetStats() { _n_recv = _n_sent = _n_send_fail = 0; }
  void powerOff() { _arb.driver()->powerOff(); }
  uint8_t getSpreadingFactor() const { return _arb.channel(PROTO_MC).sf; }

  int interferenceThreshold() const { return _threshold; }
  bool cadEnabled() const { return _cad; }

private:
#ifdef LORA_TX_POWER
  static const int8_t LORA_TX_POWER_MAX = LORA_TX_POWER;
#else
  static const int8_t LORA_TX_POWER_MAX = 22;
#endif
  RadioArbiter& _arb;
  float _last_rssi = 0, _last_snr = 0;
  uint32_t _n_recv = 0, _n_sent = 0, _n_send_fail = 0;
  int _threshold = 0;
  bool _cad = false;
};

}  // namespace arb
