// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)

#include "Console.h"
#include "ArbApp.h"
#include "ArbPlatform.h"

#include <stdlib.h>

namespace arb {

Console console;

void Console::begin() {
  _len = 0;
  _line[0] = 0;
  _had_dtr = (bool)Serial;
}

void Console::loop() {
  const uint32_t now = millis();

  // Native USB: the host closing the port is the host leaving.
  bool dtr = (bool)Serial;
  if (_had_dtr && !dtr && rnode_host.attached()) rnode_host.onFrame(rnode::CMD_LEAVE, nullptr, 0);
  _had_dtr = dtr;

  int budget = 256;   // bytes per loop: keep the radio loop turning
  while (budget-- > 0 && Serial.available() > 0) {
    uint8_t b = (uint8_t)Serial.read();
    _last_rx = now;
    uint8_t t;
    switch (_kiss.feed(b, t)) {
      case kiss::Decoder<600>::FRAME:
        rnode_host.onFrame(_kiss.command(), _kiss.payload(), _kiss.payloadLen());
        break;
      case kiss::Decoder<600>::TEXT:
        textByte(t);
        break;
      default:
        break;
    }
  }
  // An empty frame left open by a closing FEND, and nothing for a quarter
  // of a second: the KISS burst is over, what comes next may be typed text.
  // (A host writes a whole frame at once; a person types a while after.)
  if (_kiss.openAndEmpty() && (uint32_t)(now - _last_rx) > 250) _kiss.abandonFrame();

  rnode_host.tick(now);
}

void Console::textByte(uint8_t c) {
  if (c == '\r' || c == '\n') {
    if (_len == 0) return;
    _line[_len] = 0;
    Serial.print("\r\n");
    runLine(_line);
    _len = 0;
    return;
  }
  if (c == 8 || c == 127) {
    if (_len > 0) { _len--; Serial.print("\b \b"); }
    return;
  }
  if (c < 32 || _len >= sizeof(_line) - 1) return;
  _line[_len++] = (char)c;
  Serial.write(c);
}

void Console::runLine(char* line) {
  while (*line == ' ') line++;
  char reply[1024];
  reply[0] = 0;
  if (strncmp(line, "arb", 3) == 0 && (line[3] == 0 || line[3] == ' ')) {
    char* args = line + 3;
    while (*args == ' ') args++;
    arbCommand(args, reply, sizeof(reply));
  } else if (!appMeshCoreCommand(line, reply)) {
    snprintf(reply, sizeof(reply), "unknown command (arb help)");
  }
  if (reply[0]) {
    Serial.print("  -> ");
    Serial.println(reply);
  }
}

// ── `arb …` ────────────────────────────────────────────────────────────────

namespace {

bool parseOnOff(const char* s, uint8_t& out) {
  if (!strcmp(s, "on") || !strcmp(s, "1")) { out = 1; return true; }
  if (!strcmp(s, "off") || !strcmp(s, "0")) { out = 0; return true; }
  return false;
}

// 869.525 (MHz) or 869525000 (Hz).
bool parseFreq(const char* s, uint32_t& hz) {
  double v = atof(s);
  if (v <= 0) return false;
  hz = v < 10000.0 ? (uint32_t)(v * 1e6 + 0.5) : (uint32_t)v;
  return hz >= 137000000UL && hz <= 1020000000UL;
}

// 125 or 62.5 (kHz) or 125000 (Hz).
bool parseBw(const char* s, uint32_t& hz) {
  double v = atof(s);
  if (v <= 0) return false;
  hz = snapBandwidth(v < 2000.0 ? (uint32_t)(v * 1e3 + 0.5) : (uint32_t)v);
  return true;
}

}  // namespace

bool Console::arbCommand(char* args, char* reply, size_t n) {
  ArbConfig& c = app.cfg;
  char* argv[6] = {0};
  int argc = 0;
  for (char* tok = strtok(args, " "); tok && argc < 6; tok = strtok(nullptr, " ")) argv[argc++] = tok;

  if (argc == 0 || !strcmp(argv[0], "status")) { statusText(reply, n); return true; }
  if (!strcmp(argv[0], "json")) { statusJson(reply, n); return true; }

  if (!strcmp(argv[0], "help")) {
    snprintf(reply, n,
      "arb [status|json] | arb mode rnode|rns|meshcore|dual | "
      "arb rns radio <MHz>,<kHz>,<sf>,<cr> | arb rns freq|bw|sf|cr|txp <v> | "
      "arb rns transport on|off | arb rns paths <n> | arb ble [on|off|pin <n>] | arb duty <%%> | "
      "arb name <text> | arb display <s> | arb log on|off | arb reboot | arb reset. "
      "Other lines: MeshCore CLI.");
    return true;
  }

  if (!strcmp(argv[0], "mode") && argc >= 2) {
    uint8_t m;
    if (!modeFromName(argv[1], m)) { snprintf(reply, n, "modes: rnode rns meshcore dual"); return false; }
#if !ARB_WITH_RNS
    if (modeHasRns(m)) { snprintf(reply, n, "no room for Reticulum on this board: rnode or meshcore"); return false; }
#endif
    if (m == c.mode) { snprintf(reply, n, "mode already %s", modeName(m)); return true; }
    c.mode = m;
    appSaveConfig();
    snprintf(reply, n, "mode %s saved, rebooting", modeName(m));
    Serial.print("  -> "); Serial.println(reply);
    Serial.flush();
    delay(200);
    appReboot();
    return true;
  }

  if (!strcmp(argv[0], "rns")) {
    if (argc == 1) {
      snprintf(reply, n, "rns %.3f MHz %.1f kHz SF%u CR4/%u %d dBm, transport %s, paths %u",
               c.rns_freq_hz / 1e6, c.rns_bw_hz / 1e3, c.rns_sf, c.rns_cr, c.rns_txp_dbm,
               c.rns_transport ? "on" : "off", c.rns_path_table);
      return true;
    }
    if (argc < 3) { snprintf(reply, n, "arb rns <radio|freq|bw|sf|cr|txp|transport|paths> <value>"); return false; }
    const char* k = argv[1];
    const char* v = argv[2];
    ArbConfig t = c;
    bool reboot = false;
    if (!strcmp(k, "radio")) {
      char buf[48];
      strncpy(buf, v, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = 0;
      char* f = strtok(buf, ",");
      char* b = strtok(nullptr, ",");
      char* s = strtok(nullptr, ",");
      char* r = strtok(nullptr, ",");
      if (!f || !b || !s || !r || !parseFreq(f, t.rns_freq_hz) || !parseBw(b, t.rns_bw_hz)) {
        snprintf(reply, n, "arb rns radio 869.525,125,8,5");
        return false;
      }
      t.rns_sf = (uint8_t)atoi(s);
      t.rns_cr = (uint8_t)atoi(r);
    } else if (!strcmp(k, "freq")) {
      if (!parseFreq(v, t.rns_freq_hz)) { snprintf(reply, n, "bad frequency"); return false; }
    } else if (!strcmp(k, "bw")) {
      if (!parseBw(v, t.rns_bw_hz)) { snprintf(reply, n, "bad bandwidth"); return false; }
    } else if (!strcmp(k, "sf")) {
      t.rns_sf = (uint8_t)atoi(v);
    } else if (!strcmp(k, "cr")) {
      t.rns_cr = (uint8_t)atoi(v);
    } else if (!strcmp(k, "txp")) {
      int p = atoi(v);
      if (p > LORA_TX_POWER) p = LORA_TX_POWER;
      if (p < -9) p = -9;
      t.rns_txp_dbm = (int8_t)p;
    } else if (!strcmp(k, "transport")) {
      if (!parseOnOff(v, t.rns_transport)) { snprintf(reply, n, "on|off"); return false; }
      reboot = true;
    } else if (!strcmp(k, "paths")) {
      int p = atoi(v);
      if (p < 0 || p > 1000) { snprintf(reply, n, "0 (default) … 1000"); return false; }
      t.rns_path_table = (uint16_t)p;
      reboot = true;
    } else {
      snprintf(reply, n, "unknown rns setting");
      return false;
    }
    LoRaChannel ch = rnsChannelOf(t);
    if (!ch.valid()) { snprintf(reply, n, "invalid channel (SF 5-12, CR 5-8)"); return false; }
    c = t;
    appSaveConfig();
    if (reboot) {
      snprintf(reply, n, "saved, rebooting");
      Serial.print("  -> "); Serial.println(reply);
      Serial.flush();
      delay(200);
      appReboot();
      return true;
    }
    appApplyLive();
    snprintf(reply, n, "rns %.3f MHz %.1f kHz SF%u CR4/%u %d dBm", c.rns_freq_hz / 1e6, c.rns_bw_hz / 1e3,
             c.rns_sf, c.rns_cr, c.rns_txp_dbm);
    return true;
  }

  if (!strcmp(argv[0], "ble")) {
#if ARB_WITH_BLE
    if (argc == 1) {
      snprintf(reply, n, "ble %s, name \"%s\", pin %06lu, %s", c.ble ? "on" : "off", app.ble_name,
               (unsigned long)c.ble_pin, ble.connected() ? (ble.authenticated() ? "paired host" : "connected") : "idle");
      return true;
    }
    if (!strcmp(argv[1], "pin") && argc >= 3) {
      long pin = atol(argv[2]);
      if (strlen(argv[2]) != 6 || pin < 0 || pin > 999999) { snprintf(reply, n, "six digits"); return false; }
      c.ble_pin = (uint32_t)pin;
    } else if (!parseOnOff(argv[1], c.ble)) {
      snprintf(reply, n, "arb ble on|off | arb ble pin <6 digits>");
      return false;
    }
    appSaveConfig();
    Serial.println("  -> saved, rebooting");
    Serial.flush();
    delay(200);
    appReboot();
    return true;
#else
    snprintf(reply, n, "no Bluetooth LE on this board");
    return false;
#endif
  }

  if (!strcmp(argv[0], "duty") && argc >= 2) {
    double p = atof(argv[1]);
    if (p < 0 || p > 100) { snprintf(reply, n, "0 (no limit) … 100"); return false; }
    c.duty_cycle_x100 = (uint16_t)(p * 100.0 + 0.5);
    appSaveConfig();
    appApplyLive();
    snprintf(reply, n, "duty cycle %.1f %%%s", p, p == 0 ? " (no limit)" : "");
    return true;
  }

  if (!strcmp(argv[0], "log") && argc >= 2) {
    if (!parseOnOff(argv[1], c.log)) { snprintf(reply, n, "on|off"); return false; }
    appSaveConfig();
    appApplyLive();
    snprintf(reply, n, "log %s", c.log ? "on" : "off");
    return true;
  }

  if (!strcmp(argv[0], "name") && argc >= 2) {
    // The rest of the line, spaces included, was split by strtok: join back.
    char name[sizeof(c.name)] = {0};
    for (int i = 1; i < argc; i++) {
      if (i > 1) strncat(name, " ", sizeof(name) - strlen(name) - 1);
      strncat(name, argv[i], sizeof(name) - strlen(name) - 1);
    }
    for (char* q = name; *q; q++) if (*q == '"' || *q == '\\') *q = '\'';   // keeps `arb json` valid
    memcpy(c.name, name, sizeof(c.name));
    c.name[sizeof(c.name) - 1] = 0;
    appSaveConfig();
    snprintf(reply, n, "name %s", c.name);
    return true;
  }

  if (!strcmp(argv[0], "display") && argc >= 2) {
    int s = atoi(argv[1]);
    if (s < 0 || s > 255) { snprintf(reply, n, "0 (always on) … 255 s"); return false; }
    c.display_timeout = (uint8_t)s;
    appSaveConfig();
    snprintf(reply, n, "display timeout %d s", s);
    return true;
  }

  if (!strcmp(argv[0], "reboot")) {
    Serial.println("  -> rebooting");
    Serial.flush();
    delay(200);
    appReboot();
    return true;
  }

  if (!strcmp(argv[0], "reset")) {
    configDefaults(c, LORA_TX_POWER);
    appSaveConfig();
    Serial.println("  -> Arborisis settings back to defaults (MeshCore's are kept), rebooting");
    Serial.flush();
    delay(200);
    appReboot();
    return true;
  }

  snprintf(reply, n, "unknown: arb help");
  return false;
}

// ── Status ─────────────────────────────────────────────────────────────────

void Console::statusText(char* out, size_t n) {
  const ArbConfig& c = app.cfg;
  const LoRaChannel& mc = arbiter.channel(PROTO_MC);
  const LoRaChannel& rn = arbiter.channel(PROTO_RNS);
  const ChannelPlan& p = arbiter.plan();
  const ArbiterStats& s = arbiter.stats();
  const char* how = p.listen == PROTO_NONE ? "radio off"
                  : p.shared ? "one shared channel"
                  : p.peek == PROTO_NONE ? (p.listen == PROTO_MC ? "MeshCore channel" : "Reticulum channel")
                  : (p.listen == PROTO_MC ? "listen MeshCore, peek Reticulum" : "listen Reticulum, peek MeshCore");
  snprintf(out, n,
    "Arborisis Mesh %s (%s), mode %s%s | "
    "MeshCore %s: %.3f MHz %.1f kHz SF%u %d dBm, rx %lu tx %lu | "
    "Reticulum %s%s: %.3f MHz %.1f kHz SF%u CR4/%u %d dBm, rx %lu tx %lu, paths %lu%s%s | "
    "radio %s, %s%s, peeks %lu/%lu, noise %d dBm, airtime %.2f %% (limit %.1f %%)%s%s",
    ARB_VERSION, platformName(), modeName(c.mode), app.config_loaded ? "" : " (defaults)",
    app.mc_running ? "on" : "off", mc.freq_hz / 1e6, mc.bw_hz / 1e3, mc.sf, mc.txp_dbm,
    (unsigned long)s.rx[PROTO_MC], (unsigned long)s.tx[PROTO_MC],
    rns_side.stackRunning() ? "on" : "off", rns_side.hostActive() ? " + host" : "",
    rn.freq_hz / 1e6, rn.bw_hz / 1e3, rn.sf, rn.cr, rn.txp_dbm,
    (unsigned long)s.rx[PROTO_RNS], (unsigned long)s.tx[PROTO_RNS], (unsigned long)rns_side.pathCount(),
    rns_side.identityHex()[0] ? ", id " : "", rns_side.identityHex(),
    arbiter.stateName(), how, p.degraded ? " (degraded)" : "",
    (unsigned long)s.peek_hits, (unsigned long)s.peeks, arbiter.noiseFloor(),
    arbiter.airtimeLong() * 100.0f, c.duty_cycle_x100 / 100.0f,
    app.ble_name[0] ? " | BLE " : "", app.ble_name);
}

void Console::statusJson(char* out, size_t n) {
  const ArbConfig& c = app.cfg;
  const LoRaChannel& mc = arbiter.channel(PROTO_MC);
  const LoRaChannel& rn = arbiter.channel(PROTO_RNS);
  const ChannelPlan& p = arbiter.plan();
  const ArbiterStats& s = arbiter.stats();
  snprintf(out, n,
    "{\"fw\":\"%s\",\"platform\":\"%s\",\"mode\":\"%s\",\"name\":\"%s\","
    "\"mc\":{\"on\":%s,\"freq\":%lu,\"bw\":%lu,\"sf\":%u,\"txp\":%d,\"rx\":%lu,\"tx\":%lu},"
    "\"rns\":{\"on\":%s,\"host\":%s,\"freq\":%lu,\"bw\":%lu,\"sf\":%u,\"cr\":%u,\"txp\":%d,"
    "\"rx\":%lu,\"tx\":%lu,\"paths\":%lu,\"id\":\"%s\",\"transport\":%s},"
    "\"radio\":{\"state\":\"%s\",\"listen\":%d,\"peek\":%d,\"shared\":%s,\"degraded\":%s,"
    "\"peek_every_ms\":%lu,\"peeks\":%lu,\"peek_hits\":%lu,\"noise\":%d,\"airtime\":%.4f,\"duty\":%.2f,"
    "\"refusals\":%lu,\"rx_errors\":%lu},\"ble\":{\"on\":%s,\"name\":\"%s\",\"connected\":%s},\"uptime\":%lu}",
    ARB_VERSION, platformName(), modeName(c.mode), c.name,
    app.mc_running ? "true" : "false", (unsigned long)mc.freq_hz, (unsigned long)mc.bw_hz, mc.sf, mc.txp_dbm,
    (unsigned long)s.rx[PROTO_MC], (unsigned long)s.tx[PROTO_MC],
    rns_side.stackRunning() ? "true" : "false", rns_side.hostActive() ? "true" : "false",
    (unsigned long)rn.freq_hz, (unsigned long)rn.bw_hz, rn.sf, rn.cr, rn.txp_dbm,
    (unsigned long)s.rx[PROTO_RNS], (unsigned long)s.tx[PROTO_RNS], (unsigned long)rns_side.pathCount(),
    rns_side.identityHex(), c.rns_transport ? "true" : "false",
    arbiter.stateName(), p.listen, p.peek, p.shared ? "true" : "false", p.degraded ? "true" : "false",
    (unsigned long)p.peek_every_ms, (unsigned long)s.peeks, (unsigned long)s.peek_hits, arbiter.noiseFloor(),
    arbiter.airtimeLong(), c.duty_cycle_x100 / 100.0f,
    (unsigned long)s.duty_refusals, (unsigned long)s.rx_errors,
    app.ble_name[0] ? "true" : "false", app.ble_name, ble.connected() ? "true" : "false",
    (unsigned long)((millis() - app.boot_ms) / 1000));
}

}  // namespace arb
