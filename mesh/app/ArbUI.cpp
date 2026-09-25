// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)

#include "ArbUI.h"
#include "ArbApp.h"
#include "ArbPlatform.h"
#include <target.h>
#include <helpers/ui/MomentaryButton.h>

namespace arb {

ArbUI ui;

// The modes the button offers, in menu order. Without Reticulum on the
// board (STM32WL), only the two that make sense there.
#if ARB_WITH_RNS
static const uint8_t MENU_MODES[] = { MODE_DUAL, MODE_MESHCORE, MODE_RNS, MODE_RNODE };
#else
static const uint8_t MENU_MODES[] = { MODE_MESHCORE, MODE_RNODE };
#endif
static const uint8_t MENU_N = sizeof(MENU_MODES);

static const char* modeLabel(uint8_t m) {
  switch (m) {
    case MODE_DUAL:     return "MC + Reticulum";
    case MODE_MESHCORE: return "MeshCore";
    case MODE_RNS:      return "Reticulum";
    case MODE_RNODE:    return "RNode modem";
    default:            return "?";
  }
}

static uint8_t menuIndexOf(uint8_t mode) {
  for (uint8_t i = 0; i < MENU_N; i++) if (MENU_MODES[i] == mode) return i;
  return 0;
}

static uint8_t nextMode(uint8_t mode) { return MENU_MODES[(menuIndexOf(mode) + 1) % MENU_N]; }

void ArbUI::applyMode(uint8_t mode) {
  Serial.print("\r\n[arb] button: mode ");
  Serial.print(modeName(mode));
  Serial.println(", saved, restarting");
  Serial.flush();
  app.cfg.mode = mode;
  appSaveConfig();
  delay(1200);   // time to read the screen
  appReboot();
}

#ifdef DISPLAY_CLASS

static const uint8_t PAGES = 4;

void ArbUI::begin() {
#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
  display.turnOn();
  _on = true;
  _off_at = millis() + (app.cfg.display_timeout ? app.cfg.display_timeout * 1000UL : 0);
}

void ArbUI::splash(const char* line) {
  display.startFrame();
  display.setTextSize(1);
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(display.width() / 2, 4, "Arborisis Mesh");
  display.drawTextCentered(display.width() / 2, 20, "MeshCore + Reticulum");
  display.drawTextCentered(display.width() / 2, 36, line);
  display.endFrame();
}

void ArbUI::button(int ev, uint32_t now) {
  if (ev != BUTTON_EVENT_CLICK && ev != BUTTON_EVENT_LONG_PRESS) return;
  _off_at = now + (app.cfg.display_timeout ? app.cfg.display_timeout * 1000UL : 0);
  _next_refresh = now;
  if (!_on) {                      // the first press only wakes the display
    display.turnOn();
    _on = true;
    return;
  }
  if (_menu) {
    _menu_until = now + 15000;
    if (ev == BUTTON_EVENT_CLICK) {
      _menu_sel = (_menu_sel + 1) % MENU_N;
    } else if (MENU_MODES[_menu_sel] == app.cfg.mode) {
      _menu = false;               // long press on the current mode: nothing to do
    } else {
      display.startFrame();
      display.setTextSize(1);
      display.setColor(UIColor::primary_txt);
      display.drawTextCentered(display.width() / 2, 12, "Mode");
      display.drawTextCentered(display.width() / 2, 28, modeLabel(MENU_MODES[_menu_sel]));
      display.drawTextCentered(display.width() / 2, 44, "restarting...");
      display.endFrame();
      applyMode(MENU_MODES[_menu_sel]);
    }
    return;
  }
  if (ev == BUTTON_EVENT_LONG_PRESS) {
    _menu = true;
    _menu_sel = menuIndexOf(app.cfg.mode);
    _menu_until = now + 15000;
  } else {
    _page = (_page + 1) % PAGES;
  }
}

void ArbUI::loop() {
  const uint32_t now = millis();
#if defined(PIN_USER_BTN)
  button(user_btn.check(), now);
#endif
  if (_menu && (int32_t)(now - _menu_until) > 0) { _menu = false; _next_refresh = now; }
  if (!_on) return;
  if (app.cfg.display_timeout && (int32_t)(now - _off_at) > 0) {
    display.turnOff();
    _on = false;
    return;
  }
  if ((int32_t)(now - _next_refresh) < 0) return;
  _next_refresh = now + (display.isEink() && !_menu ? 60000UL : (_menu ? 250UL : 1000UL));
  display.startFrame();
  if (_menu) renderMenu(); else render();
  display.endFrame();
}

void ArbUI::renderMenu() {
  display.setTextSize(1);
  display.setColor(UIColor::primary_txt);
  display.setCursor(0, 0);
  display.print("Mode  (long = OK)");
  for (uint8_t i = 0; i < MENU_N; i++) {
    char l[40];
    const uint8_t m = MENU_MODES[i];
    snprintf(l, sizeof(l), "%c %s%s", i == _menu_sel ? '>' : ' ', modeLabel(m), m == app.cfg.mode ? " *" : "");
    display.setCursor(0, 12 + i * 11);
    display.print(l);
  }
}

void ArbUI::render() {
  char l[48];
  const int lh = 10;   // line height at text size 1
  int y = 0;
  const LoRaChannel& mc = arbiter.channel(PROTO_MC);
  const LoRaChannel& rn = arbiter.channel(PROTO_RNS);
  const ArbiterStats& s = arbiter.stats();
  const ChannelPlan& p = arbiter.plan();
  auto line = [&](const char* t) { display.setCursor(0, y); display.print(t); y += lh; };

  display.setTextSize(1);
  display.setColor(UIColor::primary_txt);
  switch (_page) {
    case 0:
      snprintf(l, sizeof(l), "%s [%s]", app.cfg.name, modeName(app.cfg.mode));
      line(l);
      snprintf(l, sizeof(l), "MC  %s %.3f SF%u", app.mc_running ? "on " : "off", mc.freq_hz / 1e6, mc.sf);
      line(l);
      snprintf(l, sizeof(l), "RNS %s %.3f SF%u", (rns_side.stackRunning() || rns_side.hostActive()) ? "on " : "off",
               rn.freq_hz / 1e6, rn.sf);
      line(l);
      snprintf(l, sizeof(l), "rx %lu/%lu tx %lu/%lu", (unsigned long)s.rx[PROTO_MC], (unsigned long)s.rx[PROTO_RNS],
               (unsigned long)s.tx[PROTO_MC], (unsigned long)s.tx[PROTO_RNS]);
      line(l);
      snprintf(l, sizeof(l), "air %.1f%% of %.0f%%", arbiter.airtimeLong() * 100.0f, app.cfg.duty_cycle_x100 / 100.0f);
      line(l);
      if (app.ble_name[0]) {
        snprintf(l, sizeof(l), "BLE %s %06lu", app.ble_name + 6, (unsigned long)app.cfg.ble_pin);
      } else {
        snprintf(l, sizeof(l), "v%s %s", ARB_VERSION, platformName());
      }
      line(l);
      break;
    case 1:
      line("MeshCore repeater");
      snprintf(l, sizeof(l), "%s", app.mc_name);
      line(l);
      snprintf(l, sizeof(l), "%.3f MHz %.1f kHz", mc.freq_hz / 1e6, mc.bw_hz / 1e3);
      line(l);
      snprintf(l, sizeof(l), "SF%u CR4/%u %d dBm", mc.sf, mc.cr, mc.txp_dbm);
      line(l);
      snprintf(l, sizeof(l), "rx %lu tx %lu nb %lu", (unsigned long)s.rx[PROTO_MC], (unsigned long)s.tx[PROTO_MC],
               (unsigned long)appNeighbourCount());
      line(l);
      snprintf(l, sizeof(l), "%.8s", app.mc_pubkey_hex);
      line(l);
      break;
    case 2:
      line(rns_side.hostActive() ? "Reticulum + host" : "Reticulum");
      snprintf(l, sizeof(l), "%.3f MHz %.1f kHz", rn.freq_hz / 1e6, rn.bw_hz / 1e3);
      line(l);
      snprintf(l, sizeof(l), "SF%u CR4/%u %d dBm", rn.sf, rn.cr, rn.txp_dbm);
      line(l);
      snprintf(l, sizeof(l), "rx %lu tx %lu paths %lu", (unsigned long)s.rx[PROTO_RNS], (unsigned long)s.tx[PROTO_RNS],
               (unsigned long)rns_side.pathCount());
      line(l);
      snprintf(l, sizeof(l), "%.16s", rns_side.identityHex()[0] ? rns_side.identityHex() : "no transport");
      line(l);
      break;
    default:
      line("Radio");
      snprintf(l, sizeof(l), "state %s nf %d", arbiter.stateName(), arbiter.noiseFloor());
      line(l);
      if (p.peek != PROTO_NONE) {
        snprintf(l, sizeof(l), "listen %s peek %s", p.listen == PROTO_MC ? "MC" : "RNS", p.peek == PROTO_MC ? "MC" : "RNS");
        line(l);
        snprintf(l, sizeof(l), "every %lums%s", (unsigned long)p.peek_every_ms, p.degraded ? " degr." : "");
        line(l);
        snprintf(l, sizeof(l), "hits %lu/%lu", (unsigned long)s.peek_hits, (unsigned long)s.peeks);
        line(l);
      } else {
        line(p.shared ? "one shared channel" : "single channel");
      }
      snprintf(l, sizeof(l), "refused %lu err %lu", (unsigned long)s.duty_refusals, (unsigned long)s.rx_errors);
      line(l);
      break;
  }
}

#else  // no display on this board

#if defined(PIN_USER_BTN) && defined(USER_BTN_PRESSED)
// The board file says which level a press is: safe to read the button.
static MomentaryButton headless_btn(PIN_USER_BTN, 1000, USER_BTN_PRESSED == LOW);
#endif

void ArbUI::begin() {
#if defined(PIN_USER_BTN) && defined(USER_BTN_PRESSED)
  headless_btn.begin();
#endif
}

void ArbUI::loop() {
#if defined(PIN_USER_BTN) && defined(USER_BTN_PRESSED)
  if (headless_btn.check() == BUTTON_EVENT_TRIPLE_CLICK) applyMode(nextMode(app.cfg.mode));
#endif
}

void ArbUI::splash(const char*) {}
void ArbUI::render() {}
void ArbUI::renderMenu() {}
void ArbUI::button(int, uint32_t) {}

#endif

}  // namespace arb
