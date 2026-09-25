// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../../LICENSE)
//
// The radio arbiter against a simulated transceiver and a simulated LoRa
// channel. The real app/RadioArbiter.cpp runs unchanged on host stand-ins
// for RadioLib and the MeshCore board (test/mocks); the stand-in radio
// follows the rules that matter for sharing it between two channels:
//
//   • a packet is received only by a receiver that was on its channel, in
//     receive mode, early enough to lock on its preamble (six symbols
//     before the preamble ends) and stayed until the packet ended;
//   • retuning, transmitting or scanning interrupts reception;
//   • a CAD detects a packet whose preamble covers the whole CAD window;
//   • nothing is received while transmitting.
//
// The tests measure how much of each network's traffic the arbiter
// catches in each mode, with a fast main loop and with one that stalls
// (Reticulum and MeshCore both verify signatures, tens of milliseconds on
// a small MCU). This is the arbiter's logic, not RF: real radios add
// noise, collisions and imperfect CAD.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#define ARB_HOST_TEST 1   // app/RadioArbiter.h takes the stand-ins in test/mocks
#include "../../app/RadioArbiter.cpp"

using namespace arb;

uint32_t g_sim_now_ms = 0;

namespace {

struct AirTx {
  uint32_t freq, bw;
  uint8_t sf, cr;
  uint32_t t0, pre_ms, dur_ms;
  std::vector<uint8_t> data;
  bool ours;
  uint16_t preamble;
};

std::vector<AirTx> g_air;

bool sameChannel(const AirTx& t, uint32_t f, uint32_t bw, uint8_t sf) { return t.freq == f && t.bw == bw && t.sf == sf; }

class SimRadio : public PhysicalLayer {
public:
  enum Mode { STBY, RX, TX, CAD } mode = STBY;
  uint32_t freq = 0, bw = 0;
  uint8_t sf = 0, cr = 5;
  uint16_t preamble = 8;
  uint32_t rx_since = 0, op_start = 0, op_end = 0;
  bool cad_hit = false;
  void (*action)() = nullptr;
  std::vector<uint8_t> rx_buf;
  size_t next_air = 0;     // first air entry that may still end in the future
  unsigned tx_count = 0, retunes = 0;

  float tsym() const { return symbolTimeMs(bw, sf); }

  void tune(float f_mhz, float bw_khz, uint8_t s, uint8_t c) {
    // RadioLib takes MHz / kHz as floats, like the arbiter passes them: round
    // back to the 100 Hz the chips resolve.
    uint32_t f = (uint32_t)lround(f_mhz * 1e4) * 100, b = (uint32_t)lround(bw_khz * 10.0) * 100;
    if (f != freq || b != bw || s != sf) retunes++;
    freq = f; bw = b; sf = s; cr = c;
    rx_since = g_sim_now_ms;   // any reprogramming restarts the demodulator
  }

  bool lockable(const AirTx& t) const {
    return rx_since + 0.0f <= t.t0 + t.pre_ms - 6.0f * tsym();
  }

  bool receivingNow() const {
    if (mode != RX) return false;
    for (size_t i = next_air; i < g_air.size(); i++) {
      const AirTx& t = g_air[i];
      if (t.ours || !sameChannel(t, freq, bw, sf)) continue;
      uint32_t now = g_sim_now_ms;
      if (now >= t.t0 + 4 * tsym() && now < t.t0 + t.dur_ms && lockable(t)) return true;
    }
    return false;
  }

  void fire() { if (action) action(); }

  // Called once per simulated millisecond.
  void tick() {
    const uint32_t now = g_sim_now_ms;
    if (mode == TX && now >= op_end) { mode = STBY; fire(); }
    if (mode == CAD && now >= op_end) {
      cad_hit = false;
      for (size_t i = next_air; i < g_air.size(); i++) {
        const AirTx& t = g_air[i];
        if (!t.ours && sameChannel(t, freq, bw, sf) && t.t0 <= op_start && t.t0 + t.pre_ms >= op_end) cad_hit = true;
      }
      mode = STBY;
      fire();
    }
    if (mode == RX) {
      for (size_t i = next_air; i < g_air.size(); i++) {
        const AirTx& t = g_air[i];
        if (t.ours || !sameChannel(t, freq, bw, sf) || t.t0 + t.dur_ms != now) continue;
        if (lockable(t)) { rx_buf = t.data; fire(); }
      }
    }
    while (next_air < g_air.size() && g_air[next_air].t0 + g_air[next_air].dur_ms + 5 < now) next_air++;
  }

  void setPacketReceivedAction(void (*f)(void)) override { action = f; }
  int16_t startReceive() override { mode = RX; rx_since = g_sim_now_ms; return RADIOLIB_ERR_NONE; }
  int16_t standby() override { mode = STBY; return RADIOLIB_ERR_NONE; }
  int16_t startChannelScan() override {
    mode = CAD;
    op_start = g_sim_now_ms;
    op_end = op_start + (uint32_t)ceilf((sf <= 8 ? 3.0f : 5.0f) * tsym());
    return RADIOLIB_ERR_NONE;
  }
  int16_t getChannelScanResult() override { return cad_hit ? RADIOLIB_LORA_DETECTED : RADIOLIB_CHANNEL_FREE; }
  int16_t startTransmit(const uint8_t* d, size_t len, uint8_t) override {
    mode = TX;
    AirTx t;
    t.freq = freq; t.bw = bw; t.sf = sf; t.cr = cr; t.t0 = g_sim_now_ms; t.preamble = preamble;
    t.pre_ms = (uint32_t)(preamble * tsym());
    t.dur_ms = (timeOnAirUs(bw, sf, cr, preamble, (uint16_t)len) + 999) / 1000;
    t.data.assign(d, d + len);
    t.ours = true;
    g_air.push_back(t);
    op_end = t.t0 + t.dur_ms;
    tx_count++;
    return RADIOLIB_ERR_NONE;
  }
  int16_t finishTransmit() override { mode = STBY; return RADIOLIB_ERR_NONE; }
  size_t getPacketLength(bool) override { return rx_buf.size(); }
  int16_t readData(uint8_t* d, size_t len) override {
    memcpy(d, rx_buf.data(), len);
    rx_buf.clear();
    return RADIOLIB_ERR_NONE;
  }
  float getRSSI() override { return -95.0f; }
  float getSNR() override { return 4.0f; }
  int16_t setPreambleLength(size_t len) override { preamble = (uint16_t)len; return RADIOLIB_ERR_NONE; }
};

class SimWrapper : public RadioLibWrapper {
public:
  SimWrapper(SimRadio& r, mesh::MainBoard& b) : RadioLibWrapper(r, b), _r(r) {}
  void setParams(float f, float bw, uint8_t sf, uint8_t cr) override { _r.tune(f, bw, sf, cr); }
  void setTxPower(int8_t) override {}
  float getCurrentRSSI() override { return -118.0f; }
protected:
  bool isReceivingPacket() override { return _r.receivingNow(); }
private:
  SimRadio& _r;
};

SimRadio* g_radio = nullptr;
RadioArbiter* g_arb = nullptr;

void step() {
  g_sim_now_ms++;
  g_radio->tick();
}

struct RnsSink : RxHandler {
  std::vector<std::vector<uint8_t>> got;
  void onRnsFrame(const uint8_t* f, uint16_t n, float, float) override { got.emplace_back(f, f + n); }
};

LoRaChannel chan(uint32_t f, uint32_t bw, uint8_t sf, uint16_t pre, uint8_t cr = 5) {
  LoRaChannel c;
  c.freq_hz = f; c.bw_hz = bw; c.sf = sf; c.cr = cr; c.txp_dbm = 20; c.preamble = pre;
  return c;
}

// Other nodes' traffic on one channel: frames one after the other, with
// exponential gaps of mean `mean_gap_ms`, tagged by their first bytes.
void schedule(const LoRaChannel& ch, uint32_t until_ms, uint32_t mean_gap_ms, uint8_t tag, int& count,
              bool rnode_shaped, unsigned seed, bool avoid_overlap = false) {
  srand(seed);
  uint32_t t = 500;
  while (true) {
    double u = (rand() + 1.0) / (RAND_MAX + 2.0);
    t += (uint32_t)(-log(u) * mean_gap_ms);
    if (avoid_overlap) {
      // One medium: a sender waits for the channel to be free (CSMA).
      for (const AirTx& o : g_air) {
        if (sameChannel(o, ch.freq_hz, ch.bw_hz, ch.sf) && t < o.t0 + o.dur_ms + 20 && t + 2000 > o.t0) {
          t = o.t0 + o.dur_ms + 20;
        }
      }
    }
    uint16_t len = 30 + rand() % 180;
    AirTx a;
    a.freq = ch.freq_hz; a.bw = ch.bw_hz; a.sf = ch.sf; a.cr = ch.cr; a.preamble = ch.preamble;
    a.t0 = t;
    a.pre_ms = (uint32_t)(ch.preamble * symbolTimeMs(ch.bw_hz, ch.sf));
    a.dur_ms = (timeOnAirUs(ch.bw_hz, ch.sf, ch.cr, ch.preamble, len) + 999) / 1000;
    if (t + a.dur_ms >= until_ms) break;
    a.data.assign(len, 0);
    if (rnode_shaped) {             // a plausible RNode/Reticulum frame
      a.data[0] = 0x40; a.data[1] = 0x00; a.data[2] = 1; a.data[19] = 0x00;
    } else {                        // a MeshCore direct text message
      a.data[0] = 0x0A;
    }
    a.data[3] = tag;
    a.data[4] = (uint8_t)count;
    a.ours = false;
    g_air.push_back(a);
    count++;
    t += a.dur_ms;
  }
}

struct Result {
  int mc_sent = 0, mc_got = 0, rns_sent = 0, rns_got = 0;
  int mc_ideal = 0, rns_ideal = 0;   // what one receiver that always knew where to listen would get
};

// The best any single receiver can do: it is always on the right channel,
// but receives one packet at a time.
void ideal(Result& r) {
  std::vector<const AirTx*> v;
  for (auto& t : g_air) if (!t.ours) v.push_back(&t);
  std::sort(v.begin(), v.end(), [](const AirTx* a, const AirTx* b) { return a->t0 < b->t0; });
  uint32_t busy = 0;
  for (const AirTx* t : v) {
    if (t->t0 < busy) continue;
    busy = t->t0 + t->dur_ms;
    if (t->data[3] == 0x4D) r.mc_ideal++; else r.rns_ideal++;
  }
}

// Runs the arbiter over `ms` of traffic. `loop_every` is the main loop's
// period; every `stall_every` ms the loop stalls for `stall_ms`.
Result run(bool mc_on, const LoRaChannel& mc, bool rns_on, const LoRaChannel& rn, uint32_t ms,
           uint32_t mc_gap, uint32_t rns_gap, uint32_t loop_every = 1, uint32_t stall_every = 0,
           uint32_t stall_ms = 0) {
  g_sim_now_ms = 0;
  g_air.clear();
  SimRadio radio;
  mesh::MainBoard board;
  SimWrapper drv(radio, board);
  RadioArbiter arbiter;
  g_radio = &radio;
  g_arb = &arbiter;
  RnsSink sink;
  Result r;
  const bool one_medium = mc.samePhy(rn);
  if (mc_on) schedule(mc, ms, mc_gap, 0x4D, r.mc_sent, false, 11, one_medium);
  if (rns_on) schedule(rn, ms, rns_gap, 0x52, r.rns_sent, true, 22, one_medium);
  std::sort(g_air.begin(), g_air.end(), [](const AirTx& a, const AirTx& b) { return a.t0 < b.t0; });
  ideal(r);

  arbiter.begin(drv, board);
  arbiter.setChannel(PROTO_MC, mc);
  arbiter.setChannel(PROTO_RNS, rn);
  arbiter.setEnabled(mc_on, rns_on);
  arbiter.setRxHandler(&sink);

  uint32_t next_loop = 0, next_stall = stall_every;
  uint8_t buf[256];
  while (g_sim_now_ms < ms) {
    if (g_sim_now_ms >= next_loop) {
      arbiter.loop();
      float rssi, snr;
      while (arbiter.mcRecv(buf, sizeof(buf), rssi, snr) > 0) r.mc_got++;
      next_loop = g_sim_now_ms + loop_every;
      if (stall_every && g_sim_now_ms >= next_stall) {
        next_loop = g_sim_now_ms + stall_ms;
        next_stall = g_sim_now_ms + stall_every;
      }
    }
    step();
  }
  r.rns_got = (int)sink.got.size();
  return r;
}

const LoRaChannel MC_EU = chan(869618000, 62500, 8, meshcorePreambleSymbols(8));
const LoRaChannel RNS_BE = chan(869525000, 125000, 8, rnodePreambleSymbols(125000, 8, 5));

double pct(int got, int sent) { return sent ? 100.0 * got / sent : 100.0; }

void report(const char* what, const Result& r) {
  printf("  %-18s MeshCore %d/%d sent, %.1f %% of the ideal %d | Reticulum %d/%d sent, %.1f %% of the ideal %d\n",
         what, r.mc_got, r.mc_sent, pct(r.mc_got, r.mc_ideal), r.mc_ideal,
         r.rns_got, r.rns_sent, pct(r.rns_got, r.rns_ideal), r.rns_ideal);
}

}  // namespace

void sim_yield() { step(); }

// ── Single protocol: the baseline ──────────────────────────────────────────

TEST(Arbiter, SingleProtocolHearsEverything) {
  Result r = run(true, MC_EU, false, RNS_BE, 600000, 3000, 3000);
  EXPECT_GT(r.mc_sent, 100);
  EXPECT_EQ(r.mc_got, r.mc_sent);
  r = run(false, MC_EU, true, RNS_BE, 600000, 3000, 3000);
  EXPECT_GT(r.rns_sent, 100);
  EXPECT_EQ(r.rns_got, r.rns_sent);
}

// ── Two channels ───────────────────────────────────────────────────────────

// One receiver cannot take two packets at once: while it receives a
// MeshCore packet, a Reticulum one on the other channel is lost, whatever
// the arbiter does. The yardstick is therefore the ideal receiver (always
// on the right channel, one packet at a time), not the number sent.

TEST(Arbiter, DualBelgianChannelsFastLoop) {
  Result r = run(true, MC_EU, true, RNS_BE, 1800000, 4000, 4000);
  report("dual, fast loop", r);
  EXPECT_GE(pct(r.mc_got, r.mc_ideal), 97.0);
  EXPECT_GE(pct(r.rns_got, r.rns_ideal), 97.0);
}

TEST(Arbiter, DualBelgianChannelsBusyLoop) {
  // A main loop that runs every 3 ms and stalls 60 ms every 2 s.
  Result r = run(true, MC_EU, true, RNS_BE, 1800000, 4000, 4000, 3, 2000, 60);
  report("dual, busy loop", r);
  EXPECT_GE(pct(r.mc_got, r.mc_ideal), 92.0);
  EXPECT_GE(pct(r.rns_got, r.rns_ideal), 92.0);
}

TEST(Arbiter, DualHeavyTraffic) {
  // Both channels busy: a frame every second on each.
  Result r = run(true, MC_EU, true, RNS_BE, 1800000, 1000, 1000);
  report("dual, heavy", r);
  EXPECT_GE(pct(r.mc_got, r.mc_ideal), 92.0);
  EXPECT_GE(pct(r.rns_got, r.rns_ideal), 92.0);
}

// ── One shared channel ─────────────────────────────────────────────────────

TEST(Arbiter, SharedChannelSortsFrames) {
  LoRaChannel mc = chan(869525000, 125000, 8, 32);
  Result r = run(true, mc, true, RNS_BE, 600000, 3000, 3000);
  report("shared channel", r);
  EXPECT_EQ(r.mc_got, r.mc_sent);
  EXPECT_EQ(r.rns_got, r.rns_sent);
}

// ── Transmitting ───────────────────────────────────────────────────────────

TEST(Arbiter, TransmitsEachProtocolOnItsChannelWithItsPreamble) {
  g_sim_now_ms = 0;
  g_air.clear();
  SimRadio radio;
  mesh::MainBoard board;
  SimWrapper drv(radio, board);
  RadioArbiter arbiter;
  g_radio = &radio;
  arbiter.begin(drv, board);
  arbiter.setChannel(PROTO_MC, MC_EU);
  arbiter.setChannel(PROTO_RNS, RNS_BE);
  arbiter.setEnabled(true, true);
  for (int i = 0; i < 5; i++) { arbiter.loop(); step(); }

  // A Reticulum packet of 400 bytes: two RNode frames, back to back.
  std::vector<uint8_t> pkt(400, 0x5A);
  ASSERT_TRUE(arbiter.rnsEnqueue(pkt.data(), (uint16_t)pkt.size()));
  for (int i = 0; i < 3000; i++) { arbiter.loop(); step(); }
  // Then a MeshCore frame, the way the Dispatcher sends it.
  uint8_t frame[60] = { 0x0A };
  bool sent = false, done = false;
  for (int i = 0; i < 5000 && !done; i++) {
    arbiter.loop();
    if (!sent && !arbiter.mcChannelBusy()) sent = arbiter.mcStartTx(frame, sizeof(frame));
    if (sent && arbiter.mcTxComplete()) { arbiter.mcTxFinished(); done = true; }
    step();
  }
  ASSERT_TRUE(done);
  std::vector<const AirTx*> ours;
  for (auto& t : g_air) if (t.ours) ours.push_back(&t);
  ASSERT_EQ(ours.size(), 3u);
  EXPECT_EQ(ours[0]->freq, RNS_BE.freq_hz);
  EXPECT_EQ(ours[0]->preamble, 18);
  EXPECT_EQ(ours[0]->data.size(), 255u);
  EXPECT_EQ(ours[0]->data[0] & 0x0F, 0x01);                 // split flag
  EXPECT_EQ(ours[1]->data.size(), 400u - 254u + 1u);
  EXPECT_EQ(ours[1]->data[0], ours[0]->data[0]);            // same header: same packet
  EXPECT_LE(ours[1]->t0, ours[0]->t0 + ours[0]->dur_ms + 5); // right after the first
  EXPECT_EQ(ours[2]->freq, MC_EU.freq_hz);
  EXPECT_EQ(ours[2]->bw, MC_EU.bw_hz);
  EXPECT_EQ(ours[2]->preamble, 32);
  EXPECT_EQ(arbiter.stats().tx[PROTO_RNS], 2u);
  EXPECT_EQ(arbiter.stats().tx[PROTO_MC], 1u);
}

TEST(Arbiter, DutyCycleBudgetIsShared) {
  g_sim_now_ms = 0;
  g_air.clear();
  SimRadio radio;
  mesh::MainBoard board;
  SimWrapper drv(radio, board);
  RadioArbiter arbiter;
  g_radio = &radio;
  arbiter.begin(drv, board);
  arbiter.setChannel(PROTO_MC, MC_EU);
  arbiter.setChannel(PROTO_RNS, RNS_BE);
  arbiter.setEnabled(true, true);
  arbiter.setDutyCycle(0.001f);            // 3.6 s per hour
  std::vector<uint8_t> pkt(200, 0x11);
  // Keep the Reticulum queue fed for ten minutes.
  for (uint32_t i = 0; i < 600000; i++) {
    if (arbiter.rnsQueueHasRoom()) arbiter.rnsEnqueue(pkt.data(), (uint16_t)pkt.size());
    arbiter.loop();
    step();
  }
  uint32_t airtime = arbiter.airtimeTotalMs(PROTO_RNS);
  EXPECT_LE(airtime, 3600u + 400u);        // the budget, plus at most one frame
  EXPECT_GT(arbiter.stats().duty_refusals, 0u);
  uint8_t frame[100] = { 0x0A };
  EXPECT_FALSE(arbiter.mcStartTx(frame, sizeof(frame)));   // MeshCore is refused too
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
