// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)

#include "RadioArbiter.h"
#include "FrameClassifier.h"

namespace arb {

namespace {

// RadioLibWrapper keeps the PhysicalLayer and a few chip-specific hooks
// protected. A pointer to member formed in a derived class reaches them on
// any wrapper instance — the variant's CustomSX1262Wrapper, CustomLR1110Wrapper…
struct WrapperAccess : public RadioLibWrapper {
  static PhysicalLayer* phy(RadioLibWrapper& w) { return w.*(&WrapperAccess::_radio); }
  static bool receiving(RadioLibWrapper& w) { return (w.*(&WrapperAccess::isReceivingPacket))(); }
  static void resetAgc(RadioLibWrapper& w) { (w.*(&WrapperAccess::doResetAGC))(); }
};

inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }

}  // namespace

RadioArbiter* RadioArbiter::_self = nullptr;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void RadioArbiter::isr() {
  if (_self) _self->_irq = true;
}

bool RadioArbiter::begin(RadioLibWrapper& driver, mesh::MainBoard& board) {
  _self = this;
  _drv = &driver;
  _board = &board;
  _phy = WrapperAccess::phy(driver);
  if (!_phy) return false;
  // One interrupt for everything the chip signals on its IRQ line: receive
  // done, transmit done, CAD done. The state says which it was.
  _phy->setPacketReceivedAction(isr);
  _air_all.reset(millis());
  _air[0].reset(millis());
  _air[1].reset(millis());
  _state = S_IDLE;
  replan();
  return true;
}

void RadioArbiter::setChannel(Proto p, const LoRaChannel& ch) {
  if (p < 0) return;
  _ch[p] = ch;
  _prog_valid = false;   // force a reprogram
  replan();
  if (_state == S_LISTEN) _state = S_IDLE;
}

void RadioArbiter::setEnabled(bool mc, bool rns) {
  _on[PROTO_MC] = mc;
  _on[PROTO_RNS] = rns;
  if (!rns) { _rns_count = 0; _rns_head = 0; }
  replan();
  if (_state == S_LISTEN || _state == S_OFF) _state = S_IDLE;
}

void RadioArbiter::replan() {
  _plan = makePlan(_on[PROTO_MC] && _ch[PROTO_MC].valid(), _ch[PROTO_MC],
                   _on[PROTO_RNS] && _ch[PROTO_RNS].valid(), _ch[PROTO_RNS]);
  _next_peek = millis();
}

const char* RadioArbiter::stateName() const {
  switch (_state) {
    case S_OFF:    return "off";
    case S_IDLE:   return "idle";
    case S_LISTEN: return "rx";
    case S_CAD:    return "cad";
    case S_DWELL:  return "dwell";
    case S_TX:     return "tx";
  }
  return "?";
}

// ── Programming the radio ──────────────────────────────────────────────────

void RadioArbiter::program(Proto p, uint16_t preamble) {
  const LoRaChannel& ch = _ch[p];
  if (!(_prog_valid && _prog.samePhy(ch) && _prog.cr == ch.cr)) {
    // The wrapper's setParams is the variant's own: frequency, SF,
    // bandwidth, coding rate, and whatever its chip needs around them.
    _drv->setParams((float)ch.freq_hz / 1e6f, (float)ch.bw_hz / 1e3f, ch.sf, ch.cr);
    _prog = ch;
    _prog.preamble = 0;
    _prog_valid = true;
  }
  if (_prog.preamble != preamble) {
    _phy->setPreambleLength(preamble);
    _prog.preamble = preamble;
  }
}

void RadioArbiter::startListen() {
  if (_plan.listen == PROTO_NONE) {
    if (_state != S_OFF) {
      _phy->standby();
      _state = S_OFF;
    }
    return;
  }
  program((Proto)_plan.listen, _plan.listen_preamble);
  _irq = false;
  int16_t err = _phy->startReceive();
  _state = (err == RADIOLIB_ERR_NONE) ? S_LISTEN : S_IDLE;
}

void RadioArbiter::startCad(Proto p, CadPurpose why) {
  program(p, _ch[p].preamble);
  _irq = false;
  int16_t err = _phy->startChannelScan();
  if (err != RADIOLIB_ERR_NONE) {
    _state = S_IDLE;
    if (why == CAD_LBT_RNS) startTx(PROTO_RNS, nullptr, 0);   // no CAD on this chip: plain ALOHA
    return;
  }
  _cad_proto = p;
  _cad_why = why;
  _cad_started = millis();
  // Up to 16 symbols of CAD on long settings, plus margin.
  _cad_timeout = (uint32_t)(symbolTimeMs(_ch[p].bw_hz, _ch[p].sf) * 18.0f) + 20;
  _state = S_CAD;
  _stats.peeks += (why == CAD_PEEK);
}

void RadioArbiter::startTx(Proto p, const uint8_t* buf, uint16_t len) {
  const uint8_t* frame = buf;
  uint16_t flen = len;
  if (p == PROTO_RNS) {
    if (buf == nullptr) {   // start of a queued packet
      const RnsSlot& s = _rns_q[_rns_head];
      rnodeSplit(s.data, s.len, randomByte(), _rns_frames);
      _rns_frame_idx = 0;
    }
    frame = _rns_frames.frames[_rns_frame_idx];
    flen = _rns_frames.frame_len[_rns_frame_idx];
  }
  program(p, _ch[p].preamble);
  if (_prog_txp != _ch[p].txp_dbm) {
    _drv->setTxPower(_ch[p].txp_dbm);
    _prog_txp = _ch[p].txp_dbm;
  }
  _board->onBeforeTransmit();
  _irq = false;
  _tx_proto = p;
  _tx_len = flen;
  _tx_started = millis();
  _tx_timeout = timeOnAirMs(_ch[p], flen) * 3 / 2 + 250;
  int16_t err = _phy->startTransmit(const_cast<uint8_t*>(frame), flen);
  _state = S_TX;
  if (err != RADIOLIB_ERR_NONE) onTxDone(false);
}

// ── Main loop ──────────────────────────────────────────────────────────────

void RadioArbiter::loop() {
  const uint32_t now = millis();

  if (_irq) {
    _irq = false;
    switch (_state) {
      case S_TX:     onTxDone(true); break;
      case S_LISTEN:
      case S_DWELL:  onRxDone(); break;
      case S_CAD:    onCadDone(); break;
      default: break;
    }
  }

  switch (_state) {
    case S_TX:
      if (elapsed(now, _tx_started, _tx_timeout)) {
        _stats.tx_timeouts++;
        _phy->standby();
        onTxDone(false);
      }
      break;
    case S_CAD:
      if (elapsed(now, _cad_started, _cad_timeout)) {
        _phy->standby();
        _state = S_IDLE;
        if (_cad_why == CAD_LBT_RNS) _rns_next_try = now + 50;
      }
      break;
    case S_DWELL:
      // Stay while a preamble or header is being received; give up at the
      // soft deadline if nothing started, at the hard one in any case.
      if ((int32_t)(now - _dwell_deadline) > 0 && !receivingPacket()) {
        _stats.dwell_misses++;
        _state = S_IDLE;
      } else if ((int32_t)(now - _dwell_hard_deadline) > 0) {
        _stats.dwell_misses++;
        _state = S_IDLE;
      }
      break;
    default:
      break;
  }

  if (_state == S_LISTEN) {
    sampleNoiseFloor(now);
    if (_rns_count > 0 && _on[PROTO_RNS] && (int32_t)(now - _rns_next_try) >= 0) {
      tryRnsTx(now);
    } else if (_plan.peek != PROTO_NONE && (int32_t)(now - _next_peek) >= 0 && !receivingPacket()) {
      _next_peek = now + _plan.peek_every_ms;
      startCad((Proto)_plan.peek, CAD_PEEK);
    }
  }

  if (_state == S_IDLE || (_state == S_OFF && _plan.listen != PROTO_NONE)) startListen();
}

void RadioArbiter::tryRnsTx(uint32_t now) {
  const RnsSlot& s = _rns_q[_rns_head];
  const LoRaChannel& ch = _ch[PROTO_RNS];
  uint32_t est = timeOnAirMs(ch, s.len > RNODE_FRAME_MAX - 1 ? RNODE_FRAME_MAX : s.len + 1);
  if (s.len > RNODE_FRAME_MAX - 1) est += timeOnAirMs(ch, s.len - (RNODE_FRAME_MAX - 1) + 1);

  if (!dutyAllows(est) || _air[PROTO_RNS].wouldExceed(now, est, _rns_lock)) {
    _stats.duty_refusals++;
    _rns_next_try = now + 1000;
    return;
  }

  const bool on_rns_now = _plan.listen == PROTO_RNS || _plan.shared;
  if (on_rns_now) {
    if (receivingPacket()) {
      // Channel busy: back off a random number of RNode CSMA slots.
      _rns_next_try = now + (1 + (randomByte() & 0x07)) * rnodeCsmaSlotMs(ch.bw_hz, ch.sf, ch.cr);
      return;
    }
    startTx(PROTO_RNS, nullptr, 0);
  } else {
    startCad(PROTO_RNS, CAD_LBT_RNS);
  }
}

// ── Events ─────────────────────────────────────────────────────────────────

void RadioArbiter::onTxDone(bool ok) {
  const uint32_t now = millis();
  _phy->finishTransmit();
  _board->onAfterTransmit();
  const Proto p = _tx_proto;
  if (ok && p >= 0) {
    // Accounted at the computed time on air, as RNode does: the loop may
    // notice the end of a transmission a little late.
    const uint32_t ms = timeOnAirMs(_ch[p], _tx_len);
    _air_all.add(now, ms);
    _air[p].add(now, ms);
    _stats.tx[p]++;
  }

  if (p == PROTO_RNS) {
    if (ok && _rns_frame_idx + 1 < _rns_frames.count) {
      // The second half follows at once, as an RNode sends it.
      _rns_frame_idx++;
      startTx(PROTO_RNS, _rns_frames.frames[_rns_frame_idx], _rns_frames.frame_len[_rns_frame_idx]);
      return;
    }
    _rns_head = (_rns_head + 1) % RNS_QUEUE_LEN;
    _rns_count--;
    // Let the channel breathe for a slot before our next packet.
    _rns_next_try = now + rnodeCsmaSlotMs(_ch[p].bw_hz, _ch[p].sf, _ch[p].cr);
    if (_rx_handler) _rx_handler->onRnsTransmitted();
  } else if (p == PROTO_MC) {
    _mc_tx_done = true;
    _mc_tx_ok = ok;
  }
  _tx_proto = PROTO_NONE;
  _state = S_IDLE;
}

void RadioArbiter::onRxDone() {
  const Proto p = (_state == S_DWELL) ? _dwell_proto : (Proto)_plan.listen;
  uint8_t buf[RNODE_FRAME_MAX + 1];
  size_t len = _phy->getPacketLength();
  if (len > 0 && len <= RNODE_FRAME_MAX) {
    int16_t err = _phy->readData(buf, len);
    if (err == RADIOLIB_ERR_NONE) {
      float rssi = _phy->getRSSI();
      float snr = _phy->getSNR();
      Proto dst = p;
      if (_plan.shared) {
        dst = looksLikeRNodeFrame(buf, (uint16_t)len, _rns_split_pending, _rns_split_seq) ? PROTO_RNS : PROTO_MC;
      }
      dispatch(dst, buf, (uint16_t)len, rssi, snr);
    } else {
      _stats.rx_errors++;
    }
  } else if (len > RNODE_FRAME_MAX) {
    _stats.rx_errors++;
  }
  // Header error, CRC error or timeout leave nothing to read; startReceive()
  // clears the chip's flags on the way back to the listen channel.
  _state = S_IDLE;
}

void RadioArbiter::enterDwell(Proto p, uint32_t now) {
  _dwell_proto = p;
  const LoRaChannel& ch = _ch[p];
  const float tsym = symbolTimeMs(ch.bw_hz, ch.sf);
  // Long enough for the rest of the longest preamble either protocol uses
  // on this channel and a header; the hard limit covers a whole frame.
  const uint16_t pre = ch.preamble > 32 ? ch.preamble : 32;
  _dwell_deadline = now + (uint32_t)(tsym * (pre + 24)) + 20;
  _dwell_hard_deadline = now + (uint32_t)(tsym * pre) + timeOnAirMs(ch, RNODE_FRAME_MAX) + 50;
  _irq = false;
  _phy->startReceive();
  _state = S_DWELL;
}

void RadioArbiter::onCadDone() {
  const uint32_t now = millis();
  int16_t res = _phy->getChannelScanResult();
  const bool busy = (res == RADIOLIB_LORA_DETECTED || res == RADIOLIB_PREAMBLE_DETECTED);
  if (busy) {
    // Something is on the air there: receive it.
    _stats.peek_hits += (_cad_why == CAD_PEEK);
    if (_cad_why == CAD_LBT_RNS) {
      const LoRaChannel& ch = _ch[PROTO_RNS];
      _rns_next_try = now + (1 + (randomByte() & 0x07)) * rnodeCsmaSlotMs(ch.bw_hz, ch.sf, ch.cr);
    }
    enterDwell(_cad_proto, now);
    return;
  }
  if (_cad_why == CAD_LBT_RNS) {
    startTx(PROTO_RNS, nullptr, 0);
    return;
  }
  _state = S_IDLE;
}

void RadioArbiter::dispatch(Proto p, const uint8_t* buf, uint16_t len, float rssi, float snr) {
  if (p < 0 || !_on[p]) return;
  _stats.rx[p]++;
  if (p == PROTO_RNS) {
    if (_rx_handler) _rx_handler->onRnsFrame(buf, len, rssi, snr);
    return;
  }
  if (_mc_count == MC_QUEUE_LEN) {   // MeshCore is behind: drop the oldest
    _mc_head = (_mc_head + 1) % MC_QUEUE_LEN;
    _mc_count--;
    _stats.queue_drops++;
  }
  McSlot& s = _mc_q[(_mc_head + _mc_count) % MC_QUEUE_LEN];
  memcpy(s.data, buf, len);
  s.len = len;
  s.rssi = rssi;
  s.snr = snr;
  _mc_count++;
}

bool RadioArbiter::receivingPacket() {
  return WrapperAccess::receiving(*_drv);
}

bool RadioArbiter::dutyAllows(uint32_t ms) {
  return !_air_all.wouldExceed(millis(), ms, _duty);
}

void RadioArbiter::sampleNoiseFloor(uint32_t now) {
  if ((int32_t)(now - _nf_next) < 0) return;
  _nf_next = now + 500;
  if (receivingPacket()) return;
  int16_t rssi = (int16_t)_drv->getCurrentRSSI();
  if (rssi > _noise_floor + 14 && _nf_n > 0) return;   // a transmission, not noise
  _nf_sum += rssi;
  if (++_nf_n >= 8) {
    _noise_floor = (int16_t)(_nf_sum / _nf_n);
    if (_noise_floor < -130) _noise_floor = -130;
    _nf_sum = 0;
    _nf_n = 0;
  }
}

int16_t RadioArbiter::currentRssi() {
  if (_state != S_LISTEN) return _noise_floor;
  return (int16_t)_drv->getCurrentRSSI();
}

uint8_t RadioArbiter::randomByte() {
  return (uint8_t)::random(0, 256);
}

// ── MeshCore side ──────────────────────────────────────────────────────────

bool RadioArbiter::mcChannelBusy() {
  if (!_on[PROTO_MC]) return true;
  if (_state == S_TX || _state == S_CAD || _state == S_DWELL) return true;
  if (_rns_count > 0 && _state == S_LISTEN && _tx_proto == PROTO_NONE &&
      (int32_t)(millis() - _rns_next_try) >= 0) {
    return true;   // a Reticulum frame is about to go: let it
  }
  const bool on_mc_now = _plan.listen == PROTO_MC || _plan.shared;
  if (on_mc_now) return _state == S_LISTEN && receivingPacket();

  // MeshCore is the peeked channel: look at it now (a CAD, ~10–20 ms).
  if (_state != S_LISTEN || receivingPacket()) return true;
  program(PROTO_MC, _ch[PROTO_MC].preamble);
  _irq = false;
  if (_phy->startChannelScan() != RADIOLIB_ERR_NONE) { _state = S_IDLE; return false; }
  _state = S_CAD;
  const uint32_t t0 = millis();
  const uint32_t limit = (uint32_t)(symbolTimeMs(_ch[PROTO_MC].bw_hz, _ch[PROTO_MC].sf) * 18.0f) + 20;
  while (!_irq && !elapsed(millis(), t0, limit)) { yield(); }
  if (!_irq) { _phy->standby(); _state = S_IDLE; return false; }
  _irq = false;
  int16_t res = _phy->getChannelScanResult();
  if (res == RADIOLIB_LORA_DETECTED || res == RADIOLIB_PREAMBLE_DETECTED) {
    enterDwell(PROTO_MC, millis());
    return true;
  }
  _state = S_IDLE;   // free: stay on the MeshCore channel for the send that follows
  return false;
}

bool RadioArbiter::mcStartTx(const uint8_t* raw, int len) {
  if (_state == S_TX || len <= 0 || len > RNODE_FRAME_MAX) return false;
  if (!dutyAllows(timeOnAirMs(_ch[PROTO_MC], (uint16_t)len))) {
    _stats.duty_refusals++;
    return false;
  }
  if (_state == S_CAD || _state == S_DWELL) _phy->standby();
  _mc_tx_done = false;
  startTx(PROTO_MC, raw, (uint16_t)len);
  if (_state != S_TX) {   // the chip refused to start
    _mc_tx_done = false;
    return false;
  }
  return true;
}

bool RadioArbiter::mcTxComplete() {
  return _mc_tx_done;
}

void RadioArbiter::mcTxFinished() {
  if (_state == S_TX && _tx_proto == PROTO_MC) {
    // MeshCore gave up waiting: abort.
    _phy->standby();
    _board->onAfterTransmit();
    _tx_proto = PROTO_NONE;
    _state = S_IDLE;
  }
  _mc_tx_done = false;
}

int RadioArbiter::mcRecv(uint8_t* out, int sz, float& rssi, float& snr) {
  if (_mc_count == 0) return 0;
  McSlot& s = _mc_q[_mc_head];
  int len = s.len > sz ? sz : s.len;
  memcpy(out, s.data, len);
  rssi = s.rssi;
  snr = s.snr;
  _mc_head = (_mc_head + 1) % MC_QUEUE_LEN;
  _mc_count--;
  return len;
}

void RadioArbiter::resetAgc() {
  if (_state != S_LISTEN || receivingPacket()) return;
  WrapperAccess::resetAgc(*_drv);
  _prog_valid = false;
  _state = S_IDLE;
}

// ── Reticulum side ─────────────────────────────────────────────────────────

bool RadioArbiter::rnsEnqueue(const uint8_t* pkt, uint16_t len) {
  if (!_on[PROTO_RNS] || len == 0 || len > RNODE_MTU) return false;
  if (_rns_count >= RNS_QUEUE_LEN) {
    _stats.queue_drops++;
    return false;
  }
  RnsSlot& s = _rns_q[(_rns_head + _rns_count) % RNS_QUEUE_LEN];
  memcpy(s.data, pkt, len);
  s.len = len;
  _rns_count++;
  return true;
}

}  // namespace arb
