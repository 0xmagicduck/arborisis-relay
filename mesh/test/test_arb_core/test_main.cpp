// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../../LICENSE)
//
// Host tests for the parts of Arborisis Mesh that decide what goes on the
// air: LoRa arithmetic, RNode framing, KISS, the RNode host protocol, the
// channel plan and the shared-channel sorter.
//
//   pio test -e arb_native

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Airtime.h"
#include "ArbConfig.h"
#include "ChannelPlan.h"
#include "FrameClassifier.h"
#include "Kiss.h"
#include "LoRaMath.h"
#include "RNodeFraming.h"
#include "RNodeHost.h"

using namespace arb;

// ── LoRa arithmetic ────────────────────────────────────────────────────────

TEST(LoRaMath, TimeOnAirMatchesSemtechCalculator) {
  // SF7 / 125 kHz / 4:5, 8 preamble symbols, 10 bytes, CRC, explicit
  // header: 41.216 ms (Semtech LoRa calculator).
  EXPECT_EQ(timeOnAirUs(125000, 7, 5, 8, 10), 41216u);
  // SF12 / 125 kHz uses low data rate optimisation (32.8 ms symbols).
  EXPECT_TRUE(lowDataRateOptimize(125000, 12));
  EXPECT_FALSE(lowDataRateOptimize(125000, 10));
}

TEST(LoRaMath, RNodePreambleIsWhatRNodesSend) {
  // RNode: max(18 symbols, ceil(24 ms / symbol)).
  EXPECT_EQ(rnodePreambleSymbols(125000, 8, 5), 18);   // Arborisis channel
  EXPECT_EQ(rnodePreambleSymbols(125000, 7, 5), 24);   // 24 / 1.024 ms
  EXPECT_EQ(rnodePreambleSymbols(62500, 8, 5), 18);
  EXPECT_EQ(rnodePreambleSymbols(250000, 7, 5), 47);   // 24 / 0.512 ms
}

TEST(LoRaMath, MeshCorePreamble) {
  EXPECT_EQ(meshcorePreambleSymbols(7), 32);
  EXPECT_EQ(meshcorePreambleSymbols(8), 32);
  EXPECT_EQ(meshcorePreambleSymbols(9), 16);
}

TEST(LoRaMath, BandwidthSnapsToChipSteps) {
  EXPECT_EQ(snapBandwidth(125000), 125000u);
  EXPECT_EQ(snapBandwidth(62500), 62500u);
  EXPECT_EQ(snapBandwidth(41666), 41700u);
  EXPECT_EQ(snapBandwidth(100000), 125000u);
}

TEST(LoRaMath, SamePhyIgnoresCodingRateAndPower) {
  LoRaChannel a, b;
  a.freq_hz = b.freq_hz = 869525000;
  a.bw_hz = b.bw_hz = 125000;
  a.sf = b.sf = 8;
  a.cr = 5; b.cr = 8;
  a.txp_dbm = 22; b.txp_dbm = 14;
  EXPECT_TRUE(a.samePhy(b));
  b.freq_hz = 869618000;
  EXPECT_FALSE(a.samePhy(b));
}

// ── RNode framing ──────────────────────────────────────────────────────────

static std::vector<uint8_t> pattern(size_t n, uint8_t seed = 1) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(seed + i * 7);
  return v;
}

TEST(RNodeFraming, SplitsExactlyLikeRNode) {
  RNodeTxFrames f;
  auto p = pattern(100);
  ASSERT_EQ(rnodeSplit(p.data(), 100, 0xA7, f), 1);
  EXPECT_EQ(f.frame_len[0], 101);
  EXPECT_EQ(f.frames[0][0], 0xA0);            // sequence nibble, no split flag

  auto q = pattern(254);
  ASSERT_EQ(rnodeSplit(q.data(), 254, 0x30, f), 1);   // 254 + header = one full frame
  EXPECT_EQ(f.frame_len[0], 255);

  auto r = pattern(255);
  ASSERT_EQ(rnodeSplit(r.data(), 255, 0x30, f), 2);
  EXPECT_EQ(f.frames[0][0], 0x31);
  EXPECT_EQ(f.frames[1][0], 0x31);
  EXPECT_EQ(f.frame_len[0], 255);
  EXPECT_EQ(f.frame_len[1], 2);

  auto m = pattern(508);
  ASSERT_EQ(rnodeSplit(m.data(), 508, 0x50, f), 2);
  EXPECT_EQ(f.frame_len[0], 255);
  EXPECT_EQ(f.frame_len[1], 255);             // no empty trailer

  auto big = pattern(509);
  EXPECT_EQ(rnodeSplit(big.data(), 509, 0, f), 0);
  EXPECT_EQ(rnodeSplit(big.data(), 0, 0, f), 0);
}

TEST(RNodeFraming, RoundTripsEveryLength) {
  RNodeReassembler rx;
  for (uint16_t len = 1; len <= RNODE_MTU; len++) {
    auto p = pattern(len, (uint8_t)len);
    RNodeTxFrames f;
    ASSERT_GT(rnodeSplit(p.data(), len, (uint8_t)(len * 16), f), 0);
    RNodeReassembler::Packet out;
    bool done = false;
    for (uint8_t i = 0; i < f.count; i++) done = rx.push(f.frames[i], f.frame_len[i], 1000, out);
    ASSERT_TRUE(done) << "len " << len;
    ASSERT_EQ(out.len, len);
    ASSERT_EQ(memcmp(out.data, p.data(), len), 0) << "len " << len;
  }
}

TEST(RNodeFraming, ShortPacketBetweenTwoHalvesIsDeliveredAndLongOneSurvives) {
  RNodeReassembler rx;
  auto big = pattern(400, 3);
  auto small = pattern(20, 9);
  RNodeTxFrames fb, fs;
  rnodeSplit(big.data(), 400, 0x70, fb);
  rnodeSplit(small.data(), 20, 0x20, fs);
  RNodeReassembler::Packet out;
  EXPECT_FALSE(rx.push(fb.frames[0], fb.frame_len[0], 0, out));
  ASSERT_TRUE(rx.push(fs.frames[0], fs.frame_len[0], 10, out));
  EXPECT_EQ(out.len, 20);
  EXPECT_EQ(memcmp(out.data, small.data(), 20), 0);
  ASSERT_TRUE(rx.push(fb.frames[1], fb.frame_len[1], 20, out));
  EXPECT_EQ(out.len, 400);
  EXPECT_EQ(memcmp(out.data, big.data(), 400), 0);
}

TEST(RNodeFraming, NewFirstHalfReplacesAnOrphan) {
  RNodeReassembler rx;
  auto a = pattern(300, 1), b = pattern(300, 2);
  RNodeTxFrames fa, fb;
  rnodeSplit(a.data(), 300, 0x10, fa);
  rnodeSplit(b.data(), 300, 0x20, fb);
  RNodeReassembler::Packet out;
  rx.push(fa.frames[0], fa.frame_len[0], 0, out);           // A's second half is lost
  EXPECT_FALSE(rx.push(fb.frames[0], fb.frame_len[0], 5, out));
  ASSERT_TRUE(rx.push(fb.frames[1], fb.frame_len[1], 6, out));
  EXPECT_EQ(memcmp(out.data, b.data(), 300), 0);
}

TEST(RNodeFraming, EmptyTrailerFromOlderRNodesIsIgnored) {
  RNodeReassembler rx;
  auto p = pattern(508);
  RNodeTxFrames f;
  rnodeSplit(p.data(), 508, 0x90, f);
  RNodeReassembler::Packet out;
  rx.push(f.frames[0], f.frame_len[0], 0, out);
  ASSERT_TRUE(rx.push(f.frames[1], f.frame_len[1], 1, out));
  uint8_t trailer[1] = { 0x91 };
  EXPECT_FALSE(rx.push(trailer, 1, 2, out));
  EXPECT_FALSE(rx.pending());
}

TEST(RNodeFraming, StaleHalfIsDropped) {
  RNodeReassembler rx(5000);
  auto a = pattern(300, 1);
  RNodeTxFrames fa;
  rnodeSplit(a.data(), 300, 0x10, fa);
  RNodeReassembler::Packet out;
  rx.push(fa.frames[0], fa.frame_len[0], 0, out);
  EXPECT_FALSE(rx.push(fa.frames[1], fa.frame_len[1], 9000, out));   // too late: a new first half
}

// ── KISS ───────────────────────────────────────────────────────────────────

struct VecSink : kiss::Sink {
  std::vector<uint8_t> v;
  void put(uint8_t b) override { v.push_back(b); }
};

TEST(Kiss, EscapesBothWays) {
  VecSink s;
  uint8_t payload[] = { 0x01, kiss::FEND, 0x02, kiss::FESC, 0x03 };
  kiss::writeFrame(s, 0x00, payload, sizeof(payload));
  std::vector<uint8_t> want = { 0xC0, 0x00, 0x01, 0xDB, 0xDC, 0x02, 0xDB, 0xDD, 0x03, 0xC0 };
  EXPECT_EQ(s.v, want);

  kiss::Decoder<64> d;
  uint8_t t;
  int frames = 0;
  for (uint8_t b : s.v) {
    if (d.feed(b, t) == kiss::Decoder<64>::KISS_FRAME) {
      frames++;
      EXPECT_EQ(d.command(), 0x00);
      ASSERT_EQ(d.payloadLen(), sizeof(payload));
      EXPECT_EQ(memcmp(d.payload(), payload, sizeof(payload)), 0);
    }
  }
  EXPECT_EQ(frames, 1);
}

TEST(Kiss, RnsDetectBurstSharesDelimiters) {
  // RNodeInterface.detect(): four frames, one FEND between two.
  std::vector<uint8_t> burst = { 0xC0, 0x08, 0x73, 0xC0, 0x50, 0x00, 0xC0, 0x48, 0x00, 0xC0, 0x49, 0x00, 0xC0 };
  kiss::Decoder<64> d;
  std::vector<uint8_t> cmds;
  uint8_t t;
  for (uint8_t b : burst) if (d.feed(b, t) == kiss::Decoder<64>::KISS_FRAME) cmds.push_back(d.command());
  EXPECT_EQ(cmds, (std::vector<uint8_t>{ 0x08, 0x50, 0x48, 0x49 }));
}

TEST(Kiss, TextOutsideFramesAndAfterAbandon) {
  kiss::Decoder<64> d;
  uint8_t t;
  std::string text;
  for (char c : std::string("ver\r")) if (d.feed((uint8_t)c, t) == kiss::Decoder<64>::KISS_TEXT) text += (char)t;
  EXPECT_EQ(text, "ver\r");
  uint8_t frame[] = { 0xC0, 0x0A, 0xFF, 0xC0 };
  for (uint8_t b : frame) d.feed(b, t);
  EXPECT_TRUE(d.openAndEmpty());
  d.abandonFrame();
  text.clear();
  for (char c : std::string("arb")) if (d.feed((uint8_t)c, t) == kiss::Decoder<64>::KISS_TEXT) text += (char)t;
  EXPECT_EQ(text, "arb");
}

// ── RNode host protocol ────────────────────────────────────────────────────

struct FakeBackend : RNodeBackend {
  LoRaChannel applied;
  bool on = false;
  std::vector<std::vector<uint8_t>> sent;
  bool left = false, attached = false;
  int8_t hostApplyRadio(const LoRaChannel& ch, bool o) override { applied = ch; on = o; return ch.txp_dbm; }
  bool hostSubmit(const uint8_t* d, uint16_t n) override { sent.emplace_back(d, d + n); return true; }
  bool hostQueueHasRoom() override { return true; }
  void hostAttached() override { attached = true; }
  void hostLeft() override { left = true; }
  void hostReset() override {}
  uint8_t randomByte() override { return 0x42; }
  void channelReport(float& a, float& b, float& c, float& d, int16_t& r, int16_t& n) override {
    a = 0.01f; b = 0.02f; c = 0; d = 0; r = -110; n = -115;
  }
  void battery(uint8_t& s, uint8_t& p) override { s = rnode::BATTERY_DISCHARGING; p = 80; }
};

struct Frame { uint8_t cmd; std::vector<uint8_t> p; };

static std::vector<Frame> parse(const std::vector<uint8_t>& bytes) {
  std::vector<Frame> out;
  kiss::Decoder<600> d;
  uint8_t t;
  for (uint8_t b : bytes) {
    if (d.feed(b, t) == kiss::Decoder<600>::KISS_FRAME)
      out.push_back({ d.command(), std::vector<uint8_t>(d.payload(), d.payload() + d.payloadLen()) });
  }
  return out;
}

static void send(RNodeHost& h, uint8_t cmd, std::vector<uint8_t> p) { h.onFrame(cmd, p.data(), p.size()); }

TEST(RNodeHost, DetectAnswersWhatRnsChecks) {
  FakeBackend be;
  VecSink out;
  RNodeIdentity id;
  RNodeHost h(be, out, id);
  send(h, rnode::CMD_DETECT, { rnode::DETECT_REQ });
  send(h, rnode::CMD_FW_VERSION, { 0x00 });
  send(h, rnode::CMD_PLATFORM, { 0x00 });
  send(h, rnode::CMD_MCU, { 0x00 });
  auto f = parse(out.v);
  ASSERT_EQ(f.size(), 4u);
  EXPECT_EQ(f[0].cmd, rnode::CMD_DETECT);
  EXPECT_EQ(f[0].p, (std::vector<uint8_t>{ rnode::DETECT_RESP }));
  EXPECT_EQ(f[1].cmd, rnode::CMD_FW_VERSION);
  ASSERT_EQ(f[1].p.size(), 2u);
  // RNS refuses anything under 1.52.
  EXPECT_TRUE(f[1].p[0] > 1 || (f[1].p[0] == 1 && f[1].p[1] >= 52));
  EXPECT_EQ(f[2].p[0], id.platform);
  EXPECT_EQ(f[3].p[0], id.mcu);
  EXPECT_TRUE(h.attached());
  EXPECT_TRUE(be.attached);
}

TEST(RNodeHost, ConfigurationIsEchoedAsApplied) {
  FakeBackend be;
  VecSink out;
  RNodeIdentity id;
  id.txp_max = 20;
  RNodeHost h(be, out, id);
  send(h, rnode::CMD_DETECT, { rnode::DETECT_REQ });
  out.v.clear();
  send(h, rnode::CMD_FREQUENCY, { 0x33, 0xD3, 0xE6, 0x08 });   // 869525000
  send(h, rnode::CMD_BANDWIDTH, { 0x00, 0x01, 0xE8, 0x48 });   // 125000
  send(h, rnode::CMD_TXPOWER, { 22 });                          // over the board's 20
  send(h, rnode::CMD_SF, { 8 });
  send(h, rnode::CMD_CR, { 5 });
  send(h, rnode::CMD_LT_ALOCK, { 0x03, 0xE8 });                 // 10.00 %
  send(h, rnode::CMD_RADIO_STATE, { rnode::RADIO_STATE_ON });
  auto f = parse(out.v);
  ASSERT_GE(f.size(), 7u);
  EXPECT_EQ(f[0].p, (std::vector<uint8_t>{ 0x33, 0xD3, 0xE6, 0x08 }));
  EXPECT_EQ(f[1].p, (std::vector<uint8_t>{ 0x00, 0x01, 0xE8, 0x48 }));
  EXPECT_EQ(f[2].p[0], 20);        // clamped: RNS will report the mismatch, as with an RNode
  EXPECT_EQ(f[3].p[0], 8);
  EXPECT_EQ(f[4].p[0], 5);
  EXPECT_EQ(f[5].p, (std::vector<uint8_t>{ 0x03, 0xE8 }));
  EXPECT_EQ(f[6].cmd, rnode::CMD_RADIO_STATE);
  EXPECT_EQ(f[6].p[0], rnode::RADIO_STATE_ON);
  EXPECT_TRUE(be.on);
  EXPECT_EQ(be.applied.freq_hz, 869525000u);
  EXPECT_EQ(be.applied.bw_hz, 125000u);
  EXPECT_EQ(be.applied.sf, 8);
  EXPECT_EQ(be.applied.preamble, 18);
  EXPECT_NEAR(h.ltLock(), 0.10f, 1e-4);
}

TEST(RNodeHost, DataBothWays) {
  FakeBackend be;
  VecSink out;
  RNodeHost h(be, out, RNodeIdentity());
  send(h, rnode::CMD_DETECT, { rnode::DETECT_REQ });
  send(h, rnode::CMD_FREQUENCY, { 0x33, 0xD3, 0xE6, 0x08 });
  send(h, rnode::CMD_BANDWIDTH, { 0x00, 0x01, 0xE8, 0x48 });
  send(h, rnode::CMD_SF, { 8 });
  send(h, rnode::CMD_CR, { 5 });
  send(h, rnode::CMD_TXPOWER, { 14 });
  send(h, rnode::CMD_RADIO_STATE, { rnode::RADIO_STATE_ON });
  send(h, rnode::CMD_DATA, { 1, 2, 3, 0xC0, 5 });
  ASSERT_EQ(be.sent.size(), 1u);
  EXPECT_EQ(be.sent[0], (std::vector<uint8_t>{ 1, 2, 3, 0xC0, 5 }));

  out.v.clear();
  uint8_t pkt[] = { 9, 8, 7 };
  h.deliver(pkt, 3, -97.0f, 6.25f);
  auto f = parse(out.v);
  ASSERT_EQ(f.size(), 3u);
  EXPECT_EQ(f[0].cmd, rnode::CMD_STAT_RSSI);
  EXPECT_EQ(f[0].p[0], (uint8_t)(-97 + 157));
  EXPECT_EQ(f[1].cmd, rnode::CMD_STAT_SNR);
  EXPECT_EQ((int8_t)f[1].p[0], 25);
  EXPECT_EQ(f[2].cmd, rnode::CMD_DATA);
  EXPECT_EQ(f[2].p, (std::vector<uint8_t>{ 9, 8, 7 }));

  send(h, rnode::CMD_LEAVE, { 0xFF });
  EXPECT_TRUE(be.left);
  EXPECT_FALSE(h.attached());
}

TEST(RNodeHost, AnnouncesResetLikeAnRNode) {
  FakeBackend be;
  VecSink out;
  RNodeHost h(be, out, RNodeIdentity());
  h.announceReset();
  auto f = parse(out.v);
  ASSERT_EQ(f.size(), 1u);
  EXPECT_EQ(f[0].cmd, rnode::CMD_RESET);
  EXPECT_EQ(f[0].p, (std::vector<uint8_t>{ 0xF8 }));
}

TEST(RNodeHost, DataRefusedWhileRadioOff) {
  FakeBackend be;
  VecSink out;
  RNodeHost h(be, out, RNodeIdentity());
  send(h, rnode::CMD_DATA, { 1, 2, 3 });
  EXPECT_TRUE(be.sent.empty());
  auto f = parse(out.v);
  ASSERT_EQ(f.size(), 1u);
  EXPECT_EQ(f[0].cmd, rnode::CMD_ERROR);
}

// ── Channel plan ───────────────────────────────────────────────────────────

static LoRaChannel chan(uint32_t f, uint32_t bw, uint8_t sf, uint16_t pre) {
  LoRaChannel c;
  c.freq_hz = f; c.bw_hz = bw; c.sf = sf; c.cr = 5; c.txp_dbm = 22; c.preamble = pre;
  return c;
}

TEST(ChannelPlan, BelgianChannelsListenReticulumPeekMeshCore) {
  LoRaChannel mc = chan(869618000, 62500, 8, meshcorePreambleSymbols(8));
  LoRaChannel rn = chan(869525000, 125000, 8, rnodePreambleSymbols(125000, 8, 5));
  ChannelPlan p = makePlan(true, mc, true, rn);
  EXPECT_EQ(p.listen, PROTO_RNS);
  EXPECT_EQ(p.peek, PROTO_MC);
  EXPECT_FALSE(p.shared);
  EXPECT_FALSE(p.degraded);
  EXPECT_NEAR((double)p.peek_every_ms, 43.0, 2.0);
  EXPECT_LT(p.peek_cost_ms, 20u);
  EXPECT_EQ(p.listen_preamble, 18);
}

TEST(ChannelPlan, SameChannelIsShared) {
  LoRaChannel mc = chan(869525000, 125000, 8, 32);
  LoRaChannel rn = chan(869525000, 125000, 8, 18);
  ChannelPlan p = makePlan(true, mc, true, rn);
  EXPECT_TRUE(p.shared);
  EXPECT_EQ(p.peek, PROTO_NONE);
  EXPECT_EQ(p.listen_preamble, 18);
}

TEST(ChannelPlan, OneProtocolListensOnly) {
  LoRaChannel mc = chan(869618000, 62500, 8, 32);
  LoRaChannel rn = chan(869525000, 125000, 8, 18);
  ChannelPlan p = makePlan(false, mc, true, rn);
  EXPECT_EQ(p.listen, PROTO_RNS);
  EXPECT_EQ(p.peek, PROTO_NONE);
  p = makePlan(false, mc, false, rn);
  EXPECT_EQ(p.listen, PROTO_NONE);
}

TEST(ChannelPlan, FastListenChannelIsFlaggedDegraded) {
  // Reticulum at SF7 / 500 kHz: a 24-symbol preamble lasts 6 ms, shorter
  // than any peek at a 62.5 kHz MeshCore channel.
  LoRaChannel mc = chan(869618000, 62500, 8, 32);
  LoRaChannel rn = chan(869525000, 500000, 7, 24);
  ChannelPlan p = makePlan(true, mc, true, rn);
  EXPECT_EQ(p.listen, PROTO_RNS);
  EXPECT_TRUE(p.degraded);
}

// ── Shared-channel sorter ──────────────────────────────────────────────────

static std::vector<uint8_t> rnsFrame(uint8_t flags, uint8_t hops, uint8_t ctx, size_t data_len) {
  std::vector<uint8_t> f = { 0x40, flags, hops };
  bool h2 = flags & 0x40;
  for (int i = 0; i < (h2 ? 32 : 16); i++) f.push_back((uint8_t)(0x11 * i));
  f.push_back(ctx);
  for (size_t i = 0; i < data_len; i++) f.push_back((uint8_t)i);
  return f;
}

TEST(FrameClassifier, ReticulumFramesAreRecognised) {
  auto announce = rnsFrame(0x01, 0, 0x00, 160);      // HEADER_1, SINGLE, ANNOUNCE
  EXPECT_TRUE(looksLikeRNodeFrame(announce.data(), announce.size(), false, 0));
  auto data = rnsFrame(0x00, 2, 0x00, 40);           // HEADER_1 DATA
  EXPECT_TRUE(looksLikeRNodeFrame(data.data(), data.size(), false, 0));
  auto transported = rnsFrame(0x50, 3, 0xFE, 10);    // HEADER_2, TRANSPORT, DATA, LRRTT
  EXPECT_TRUE(looksLikeRNodeFrame(transported.data(), transported.size(), false, 0));
  uint8_t second_half[] = { 0x41, 1, 2, 3 };
  EXPECT_TRUE(looksLikeRNodeFrame(second_half, 4, true, 0x4));
}

TEST(FrameClassifier, MeshCoreFramesMostlyAreNot) {
  // A MeshCore frame whose header byte is not RNode-shaped is never taken.
  uint8_t txt[40] = { 0x0A };      // direct TXT_MSG
  EXPECT_FALSE(looksLikeRNodeFrame(txt, sizeof(txt), false, 0));
  // Zero-hop flood adverts (header 0x11, path_len 0) with random keys: the
  // sorter must keep nearly all of them for MeshCore.
  int taken = 0;
  uint32_t x = 12345;
  for (int i = 0; i < 10000; i++) {
    uint8_t adv[110];
    adv[0] = 0x11;
    adv[1] = 0x00;
    for (size_t k = 2; k < sizeof(adv); k++) { x = x * 1103515245u + 12345u; adv[k] = (uint8_t)(x >> 16); }
    taken += looksLikeRNodeFrame(adv, sizeof(adv), false, 0);
  }
  EXPECT_LT(taken, 200);   // under 2 %
}

// ── Airtime and configuration ──────────────────────────────────────────────

TEST(Airtime, LongTermShareAndLimit) {
  AirtimeMeter m;
  m.reset(0);
  m.add(1000, 36000);                      // 36 s in the first minute
  EXPECT_NEAR(m.longTerm(60000), 0.01, 1e-4);
  EXPECT_FALSE(m.wouldExceed(60000, 300000, 0.10f));
  EXPECT_TRUE(m.wouldExceed(60000, 330000, 0.10f));
  EXPECT_FALSE(m.wouldExceed(60000, 10000000, 0.0f));   // no limit
  EXPECT_NEAR(m.longTerm(60000 + 3600000), 0.0, 1e-6);  // an hour later it is gone
}

TEST(Config, DefaultsAreTheArborisisChannelAndValid) {
  ArbConfig c;
  configDefaults(c, 20);
  EXPECT_TRUE(configValid(c));
  EXPECT_EQ(c.rns_freq_hz, 869525000u);
  EXPECT_EQ(c.rns_bw_hz, 125000u);
  EXPECT_EQ(c.rns_sf, 8);
  EXPECT_EQ(c.rns_txp_dbm, 20);            // clamped to the board
  EXPECT_EQ(c.duty_cycle_x100, 1000);
  EXPECT_EQ(c.mode, MODE_DUAL);
  c.rns_sf = 13;
  c.crc = configCrc(c);
  EXPECT_FALSE(configValid(c));
  configDefaults(c, 22);
  c.mode = MODE_RNS;                        // changed without a new CRC
  EXPECT_FALSE(configValid(c));
}

TEST(Config, ModeNames) {
  uint8_t m;
  for (uint8_t i = 0; i <= MODE_DUAL; i++) {
    ASSERT_TRUE(modeFromName(modeName(i), m));
    EXPECT_EQ(m, i);
  }
  EXPECT_FALSE(modeFromName("bogus", m));
  EXPECT_TRUE(modeHasMeshCore(MODE_DUAL) && modeHasRns(MODE_DUAL));
  EXPECT_FALSE(modeHasRns(MODE_RNODE) || modeHasMeshCore(MODE_RNODE));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
