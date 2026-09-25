// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../../LICENSE)
//
// rnode_air — two Arborisis RNode host ends joined by a simulated LoRa
// channel, each on a pseudo-terminal. Real Reticulum instances (RNS's own
// RNodeInterface) open the two terminals as if they were two RNodes.
//
// What crosses the "air" goes through the firmware's own code: RNodeHost
// (the protocol RNS speaks), rnodeSplit (the frames a packet becomes) and
// RNodeReassembler (the packet the frames become again). A frame only
// reaches the other side when both radios are on and tuned alike, as on
// real LoRa.
//
//   g++ -std=c++17 -I ../../app rnode_air.cpp -o rnode_air
//   ./rnode_air /tmp/rnodeA /tmp/rnodeB [seconds]

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "Kiss.h"
#include "RNodeFraming.h"
#include "RNodeHost.h"

using namespace arb;

static volatile sig_atomic_t stop = 0;
static void onTerm(int) { stop = 1; }

static uint32_t now_ms() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

struct FdSink : kiss::Sink {
  int fd = -1;
  uint8_t buf[4096];
  size_t n = 0;
  void put(uint8_t b) override { if (n < sizeof(buf)) buf[n++] = b; }
  void flush() override {
    size_t off = 0;
    while (off < n) {
      ssize_t w = write(fd, buf + off, n - off);
      if (w <= 0) break;
      off += w;
    }
    n = 0;
  }
};

struct Side;
static Side* sides[2];
static unsigned long frames_on_air = 0, split_packets = 0, packets_delivered = 0, mismatched = 0;

struct Side : RNodeBackend {
  int idx;
  FdSink sink;
  RNodeHost* host = nullptr;
  LoRaChannel ch;
  bool on = false;
  RNodeReassembler rx;
  kiss::Decoder<600> dec;

  int8_t hostApplyRadio(const LoRaChannel& c, bool o) override { ch = c; on = o; return c.txp_dbm; }
  bool hostQueueHasRoom() override { return true; }
  void hostLeft() override { on = false; }
  void hostReset() override {}
  uint8_t randomByte() override { return (uint8_t)rand(); }
  void channelReport(float& a, float& b, float& c, float& d, int16_t& r, int16_t& n) override {
    a = b = c = d = 0; r = -120; n = -120;
  }
  void battery(uint8_t& s, uint8_t& p) override { s = rnode::BATTERY_UNKNOWN; p = 0; }

  bool hostSubmit(const uint8_t* data, uint16_t len) override {
    Side* other = sides[1 - idx];
    RNodeTxFrames f;
    rnodeSplit(data, len, (uint8_t)rand(), f);
    if (f.count > 1) split_packets++;
    for (uint8_t i = 0; i < f.count; i++) {
      frames_on_air++;
      if (!(on && other->on && ch.samePhy(other->ch))) { mismatched++; continue; }
      RNodeReassembler::Packet p;
      if (other->rx.push(f.frames[i], f.frame_len[i], now_ms(), p)) {
        other->host->deliver(p.data, p.len, -80.0f, 8.0f);
        packets_delivered++;
      }
    }
    host->transmitted();
    return true;
  }
};

static int openPty(const char* link) {
  int m = posix_openpt(O_RDWR | O_NOCTTY);
  if (m < 0 || grantpt(m) || unlockpt(m)) { perror("pty"); exit(1); }
  const char* slave = ptsname(m);
  int s = open(slave, O_RDWR | O_NOCTTY);   // keep one end open: no EIO while RNS reconnects
  termios t;
  tcgetattr(s, &t);
  cfmakeraw(&t);
  tcsetattr(s, TCSANOW, &t);
  unlink(link);
  if (symlink(slave, link)) { perror("symlink"); exit(1); }
  fcntl(m, F_SETFL, O_NONBLOCK);
  return m;
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s linkA linkB [seconds]\n", argv[0]); return 2; }
  int seconds = argc > 3 ? atoi(argv[3]) : 120;
  Side a, b;
  a.idx = 0; b.idx = 1;
  sides[0] = &a; sides[1] = &b;
  a.sink.fd = openPty(argv[1]);
  b.sink.fd = openPty(argv[2]);
  RNodeIdentity id;
  RNodeHost ha(a, a.sink, id), hb(b, b.sink, id);
  a.host = &ha; b.host = &hb;
  signal(SIGTERM, onTerm);
  signal(SIGINT, onTerm);
  printf("ready %s %s\n", argv[1], argv[2]);
  fflush(stdout);

  const uint32_t end = now_ms() + seconds * 1000;
  while (!stop && (int32_t)(now_ms() - end) < 0) {
    pollfd p[2] = { { a.sink.fd, POLLIN, 0 }, { b.sink.fd, POLLIN, 0 } };
    poll(p, 2, 50);
    for (int i = 0; i < 2; i++) {
      Side& s = *sides[i];
      uint8_t buf[512];
      ssize_t n = read(s.sink.fd, buf, sizeof(buf));
      for (ssize_t k = 0; k < n; k++) {
        uint8_t t;
        if (s.dec.feed(buf[k], t) == kiss::Decoder<600>::KISS_FRAME)
          s.host->onFrame(s.dec.command(), s.dec.payload(), s.dec.payloadLen());
      }
      s.host->tick(now_ms());
    }
  }
  printf("frames_on_air=%lu split_packets=%lu delivered=%lu mismatched=%lu attachedA=%d attachedB=%d\n",
         frames_on_air, split_packets, packets_delivered, mismatched, ha.attached(), hb.attached());
  return 0;
}
