// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// RNodeHost.h — the device side of the RNode host protocol, so that a
// Reticulum instance on a computer or a phone (rnsd, Sideband, MeshChat,
// NomadNet) uses this board as an RNodeInterface over USB or BLE.
//
// What RNS needs, and all this implements (RNS/Interfaces/RNodeInterface.py):
//   detect  → CMD_DETECT answers DETECT_RESP, CMD_FW_VERSION ≥ 1.52,
//             CMD_PLATFORM, CMD_MCU;
//   config  → each of frequency, bandwidth, TX power, SF, CR, airtime locks
//             and radio state is echoed with the value actually applied
//             (RNS aborts when an echo differs from what it asked);
//   data    → CMD_DATA both ways, each received packet preceded by its
//             RSSI and SNR; CMD_READY for hosts with flow control;
//   status  → channel, PHY, CSMA and battery reports every few seconds.
//
// No Arduino here: the backend interface is what the firmware provides,
// the tests provide a fake one.

#pragma once

#include <stdint.h>
#include <string.h>
#include "Kiss.h"
#include "LoRaMath.h"

namespace arb {

namespace rnode {
  static const uint8_t CMD_DATA        = 0x00;
  static const uint8_t CMD_FREQUENCY   = 0x01;
  static const uint8_t CMD_BANDWIDTH   = 0x02;
  static const uint8_t CMD_TXPOWER     = 0x03;
  static const uint8_t CMD_SF          = 0x04;
  static const uint8_t CMD_CR          = 0x05;
  static const uint8_t CMD_RADIO_STATE = 0x06;
  static const uint8_t CMD_RADIO_LOCK  = 0x07;
  static const uint8_t CMD_DETECT      = 0x08;
  static const uint8_t CMD_LEAVE       = 0x0A;
  static const uint8_t CMD_ST_ALOCK    = 0x0B;
  static const uint8_t CMD_LT_ALOCK    = 0x0C;
  static const uint8_t CMD_READY       = 0x0F;
  static const uint8_t CMD_STAT_RX     = 0x21;
  static const uint8_t CMD_STAT_TX     = 0x22;
  static const uint8_t CMD_STAT_RSSI   = 0x23;
  static const uint8_t CMD_STAT_SNR    = 0x24;
  static const uint8_t CMD_STAT_CHTM   = 0x25;
  static const uint8_t CMD_STAT_PHYPRM = 0x26;
  static const uint8_t CMD_STAT_BAT    = 0x27;
  static const uint8_t CMD_STAT_CSMA   = 0x28;
  static const uint8_t CMD_STAT_TEMP   = 0x29;
  static const uint8_t CMD_BLINK       = 0x30;
  static const uint8_t CMD_RANDOM      = 0x40;
  static const uint8_t CMD_BT_CTRL     = 0x46;
  static const uint8_t CMD_BOARD       = 0x47;
  static const uint8_t CMD_PLATFORM    = 0x48;
  static const uint8_t CMD_MCU         = 0x49;
  static const uint8_t CMD_FW_VERSION  = 0x50;
  static const uint8_t CMD_RESET       = 0x55;
  static const uint8_t CMD_ERROR       = 0x90;

  static const uint8_t DETECT_REQ      = 0x73;
  static const uint8_t DETECT_RESP     = 0x46;
  static const uint8_t RESET_BYTE      = 0xF8;

  static const uint8_t RADIO_STATE_OFF = 0x00;
  static const uint8_t RADIO_STATE_ON  = 0x01;
  static const uint8_t RADIO_STATE_ASK = 0xFF;

  static const uint8_t ERROR_INITRADIO     = 0x01;
  static const uint8_t ERROR_TXFAILED      = 0x02;
  static const uint8_t ERROR_QUEUE_FULL    = 0x04;
  static const uint8_t ERROR_MEMORY_LOW    = 0x05;

  // Platform / MCU bytes. ESP32 and nRF52 are RNode's own; RP2040 and STM32
  // have none upstream (RNS only uses the byte to decide whether the device
  // has a display and whether a reset means an ESP32 reboot).
  static const uint8_t PLATFORM_ESP32  = 0x80;
  static const uint8_t PLATFORM_NRF52  = 0x70;
  static const uint8_t PLATFORM_RP2040 = 0x60;
  static const uint8_t PLATFORM_STM32  = 0x50;
  static const uint8_t MCU_ESP32       = 0x81;
  static const uint8_t MCU_NRF52       = 0x71;
  static const uint8_t MCU_RP2040      = 0x61;
  static const uint8_t MCU_STM32       = 0x51;

  static const uint8_t RSSI_OFFSET     = 157;

  static const uint8_t BATTERY_UNKNOWN     = 0x00;
  static const uint8_t BATTERY_DISCHARGING = 0x01;
  static const uint8_t BATTERY_CHARGING    = 0x02;
  static const uint8_t BATTERY_CHARGED     = 0x03;
}

// What the host protocol asks of the firmware.
class RNodeBackend {
public:
  virtual ~RNodeBackend() {}
  // Apply the host's channel (on = radio state ON). Returns the TX power
  // really set (the board may clamp it).
  virtual int8_t hostApplyRadio(const LoRaChannel& ch, bool on) = 0;
  // Queue one Reticulum packet for LoRa. False when the queue is full.
  virtual bool hostSubmit(const uint8_t* data, uint16_t len) = 0;
  virtual bool hostQueueHasRoom() = 0;
  virtual void hostAttached() {}
  virtual void hostLeft() = 0;
  virtual void hostReset() = 0;
  virtual void hostBluetooth(uint8_t ctrl) { (void)ctrl; }
  virtual uint8_t randomByte() = 0;
  // Channel report: airtime and utilisation are 0.0 … 1.0, RSSI in dBm.
  virtual void channelReport(float& airtime_short, float& airtime_long, float& load_short, float& load_long,
                             int16_t& current_rssi, int16_t& noise_floor) = 0;
  virtual void battery(uint8_t& state, uint8_t& percent) = 0;
};

struct RNodeIdentity {
  uint8_t fw_major = 1;
  uint8_t fw_minor = 82;
  uint8_t platform = rnode::PLATFORM_ESP32;
  uint8_t mcu      = rnode::MCU_ESP32;
  uint8_t board    = 0xAB;     // "Arborisis": not an RNode board code
  int8_t  txp_max  = 22;
};

class RNodeHost {
public:
  RNodeHost(RNodeBackend& backend, kiss::Sink& sink, const RNodeIdentity& id)
    : _be(backend), _out(sink), _id(id) {}

  // Channel the device starts with, before any host speaks.
  void setDefaults(const LoRaChannel& ch) { _ch = ch; }

  bool attached() const { return _attached; }
  bool radioOn() const { return _radio_on; }
  const LoRaChannel& channel() const { return _ch; }
  float stLock() const { return _st_alock; }   // 0.0 … 1.0, 0 = none
  float ltLock() const { return _lt_alock; }

  // One complete KISS frame from the host.
  void onFrame(uint8_t cmd, const uint8_t* p, size_t n) {
    using namespace rnode;
    switch (cmd) {
      case CMD_DATA:
        if (n == 0) return;
        if (!_radio_on) { error(ERROR_TXFAILED); return; }
        if (!_be.hostSubmit(p, (uint16_t)n)) { error(ERROR_QUEUE_FULL); return; }
        _tx_count++;
        return;

      case CMD_DETECT:
        if (n >= 1 && p[0] == DETECT_REQ) {
          byte(CMD_DETECT, DETECT_RESP);
          if (!_attached) { _attached = true; _be.hostAttached(); }
        }
        return;

      case CMD_FW_VERSION: { uint8_t v[2] = { _id.fw_major, _id.fw_minor }; frame(CMD_FW_VERSION, v, 2); return; }
      case CMD_PLATFORM:   byte(CMD_PLATFORM, _id.platform); return;
      case CMD_MCU:        byte(CMD_MCU, _id.mcu); return;
      case CMD_BOARD:      byte(CMD_BOARD, _id.board); return;

      case CMD_FREQUENCY:
        if (n >= 4) {
          uint32_t f = be32(p);
          if (f >= 137000000UL && f <= 3000000000UL) { _ch.freq_hz = f; apply(); }
        }
        u32(CMD_FREQUENCY, _ch.freq_hz);
        return;

      case CMD_BANDWIDTH:
        if (n >= 4) {
          uint32_t bw = be32(p);
          if (bw >= 7800 && bw <= 1625000) { _ch.bw_hz = snapBandwidth(bw); apply(); }
        }
        u32(CMD_BANDWIDTH, _ch.bw_hz);
        return;

      case CMD_TXPOWER:
        if (n >= 1 && p[0] != 0xFF) {
          int8_t txp = (int8_t)p[0];
          if (txp > _id.txp_max) txp = _id.txp_max;
          _ch.txp_dbm = txp;
          apply();
        }
        byte(CMD_TXPOWER, (uint8_t)_ch.txp_dbm);
        return;

      case CMD_SF:
        if (n >= 1 && p[0] != 0xFF) {
          uint8_t sf = p[0];
          if (sf < 5) sf = 5;
          if (sf > 12) sf = 12;
          _ch.sf = sf;
          apply();
        }
        byte(CMD_SF, _ch.sf);
        return;

      case CMD_CR:
        if (n >= 1 && p[0] != 0xFF) {
          uint8_t cr = p[0];
          if (cr < 5) cr = 5;
          if (cr > 8) cr = 8;
          _ch.cr = cr;
          apply();
        }
        byte(CMD_CR, _ch.cr);
        return;

      case CMD_RADIO_STATE:
        if (n >= 1 && p[0] != RADIO_STATE_ASK) {
          _radio_on = (p[0] == RADIO_STATE_ON) && _ch.valid();
          if (!_attached) { _attached = true; _be.hostAttached(); }
          apply();
          if (p[0] == RADIO_STATE_ON && !_radio_on) error(ERROR_INITRADIO);
        }
        byte(CMD_RADIO_STATE, _radio_on ? RADIO_STATE_ON : RADIO_STATE_OFF);
        if (_radio_on) phyReport();
        return;

      case CMD_RADIO_LOCK:
        byte(CMD_RADIO_LOCK, _ch.valid() ? 0x00 : 0x01);
        return;

      case CMD_ST_ALOCK:
        if (n >= 2) _st_alock = (float)be16(p) / 10000.0f;
        u16(CMD_ST_ALOCK, (uint16_t)(_st_alock * 10000.0f + 0.5f));
        return;

      case CMD_LT_ALOCK:
        if (n >= 2) _lt_alock = (float)be16(p) / 10000.0f;
        u16(CMD_LT_ALOCK, (uint16_t)(_lt_alock * 10000.0f + 0.5f));
        return;

      case CMD_LEAVE:
        _attached = false;
        _radio_on = false;
        _be.hostLeft();
        return;

      case CMD_READY:
        if (_be.hostQueueHasRoom()) ready();
        return;

      case CMD_STAT_RX: u32(CMD_STAT_RX, _rx_count); return;
      case CMD_STAT_TX: u32(CMD_STAT_TX, _tx_count); return;
      case CMD_STAT_BAT: batteryReport(); return;

      case CMD_RANDOM: byte(CMD_RANDOM, _be.randomByte()); return;

      case CMD_RESET:
        if (n >= 1 && p[0] == RESET_BYTE) _be.hostReset();
        return;

      case CMD_BT_CTRL:
        if (n >= 1) _be.hostBluetooth(p[0]);
        return;

      default:
        // Framebuffer, display mirroring, ROM, firmware hashes, WiFi…:
        // rnodeconf's and the display's business, not RNS's. Unanswered, the
        // host keeps its previous value.
        return;
    }
  }

  // A packet came in over LoRa (already reassembled).
  void deliver(const uint8_t* data, uint16_t len, float rssi, float snr) {
    if (!_attached || !_radio_on) return;
    int r = (int)floorf(rssi + 0.5f) + rnode::RSSI_OFFSET;
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    byte(rnode::CMD_STAT_RSSI, (uint8_t)r);
    int s = (int)(snr * 4.0f);
    if (s < -128) s = -128;
    if (s > 127) s = 127;
    byte(rnode::CMD_STAT_SNR, (uint8_t)(int8_t)s);
    frame(rnode::CMD_DATA, data, len);
    _rx_count++;
  }

  // A queued packet went out: hosts with flow control wait for this.
  void transmitted() { if (_attached && _be.hostQueueHasRoom()) ready(); }

  // Periodic reports; call every loop, it paces itself.
  void tick(uint32_t now_ms) {
    if (!_attached || !_radio_on) return;
    if ((uint32_t)(now_ms - _last_report) < 2500) return;
    _last_report = now_ms;
    channelReport();
    csmaReport();
    batteryReport();
  }

  void error(uint8_t code) { byte(rnode::CMD_ERROR, code); }

private:
  void apply() {
    if (_ch.valid()) {
      _ch.preamble = rnodePreambleSymbols(_ch.bw_hz, _ch.sf, _ch.cr);
      int8_t applied = _be.hostApplyRadio(_ch, _radio_on);
      _ch.txp_dbm = applied;
    } else if (_radio_on) {
      _radio_on = false;
      _be.hostApplyRadio(_ch, false);
    }
  }

  void channelReport() {
    float ats, atl, cls, cll;
    int16_t crs, nfl;
    _be.channelReport(ats, atl, cls, cll, crs, nfl);
    uint16_t v[4] = { pct(ats), pct(atl), pct(cls), pct(cll) };
    uint8_t b[11];
    for (int i = 0; i < 4; i++) { b[i * 2] = v[i] >> 8; b[i * 2 + 1] = v[i] & 0xFF; }
    b[8] = rssiByte(crs);
    b[9] = rssiByte(nfl);
    b[10] = 0xFF;   // no interference reported
    frame(rnode::CMD_STAT_CHTM, b, sizeof(b));
  }

  void phyReport() {
    const float tsym = symbolTimeMs(_ch.bw_hz, _ch.sf);
    const uint16_t lst = (uint16_t)(tsym * 1000.0f);
    const uint16_t lsr = tsym > 0 ? (uint16_t)(1000.0f / tsym) : 0;
    const uint16_t prs = _ch.preamble;
    const uint16_t prt = (uint16_t)ceilf(_ch.preamble * tsym);
    const uint16_t cst = (uint16_t)rnodeCsmaSlotMs(_ch.bw_hz, _ch.sf, _ch.cr);
    const uint16_t dft = (uint16_t)(2 * cst);
    uint16_t v[6] = { lst, lsr, prs, prt, cst, dft };
    uint8_t b[12];
    for (int i = 0; i < 6; i++) { b[i * 2] = v[i] >> 8; b[i * 2 + 1] = v[i] & 0xFF; }
    frame(rnode::CMD_STAT_PHYPRM, b, sizeof(b));
  }

  void csmaReport() {
    uint8_t b[3] = { 1, 0, 15 };
    frame(rnode::CMD_STAT_CSMA, b, sizeof(b));
  }

  void batteryReport() {
    uint8_t state = rnode::BATTERY_UNKNOWN, percent = 0;
    _be.battery(state, percent);
    uint8_t b[2] = { state, percent };
    frame(rnode::CMD_STAT_BAT, b, sizeof(b));
  }

  void ready() { byte(rnode::CMD_READY, 0x01); }

  static uint16_t pct(float share) {
    if (share < 0) share = 0;
    if (share > 1) share = 1;
    return (uint16_t)(share * 10000.0f);
  }
  static uint8_t rssiByte(int16_t rssi) {
    int r = rssi + rnode::RSSI_OFFSET;
    return (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
  }
  static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
  }
  static uint16_t be16(const uint8_t* p) { return ((uint16_t)p[0] << 8) | p[1]; }

  void frame(uint8_t cmd, const uint8_t* p, size_t n) { kiss::writeFrame(_out, cmd, p, n); _out.flush(); }
  void byte(uint8_t cmd, uint8_t v) { frame(cmd, &v, 1); }
  void u16(uint8_t cmd, uint16_t v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; frame(cmd, b, 2); }
  void u32(uint8_t cmd, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    frame(cmd, b, 4);
  }

  RNodeBackend& _be;
  kiss::Sink& _out;
  RNodeIdentity _id;
  LoRaChannel _ch;
  bool _attached = false;
  bool _radio_on = false;
  float _st_alock = 0, _lt_alock = 0;
  uint32_t _rx_count = 0, _tx_count = 0;
  uint32_t _last_report = 0;
};

}  // namespace arb
