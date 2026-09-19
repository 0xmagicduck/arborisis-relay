// Copyright (C) 2026, Arborisis
// Arborisis Relay — the RTNode firmware, pre-tuned for the Arborisis Belgium
// LoRa network (https://rns.arborisis.com).
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Arborisis.h — the build profile of an Arborisis relay.
//
// Everything that makes an Arborisis relay different from a stock RTNode is a
// macro in this file, and nothing else in the tree hard-codes a value of the
// network: the rest of the firmware reads these macros through ARBORISIS_*
// defaults that fall back to upstream behaviour when ARBORISIS_RELAY is not
// defined. Two consequences we care about:
//
//   1. The upstream environments (rtnode_heltec_v3 / _v4) still build the
//      unmodified RTNode, so rebasing on jrl290/RTNode-HeltecV4 stays cheap.
//   2. A change of channel is a change *here*, then a rebuild — not a hunt
//      through Display.h, FirewallConfig.h and the .ino for a frequency.
//
// The radio parameters below are the ones the Arborisis Belgium gateway
// publishes (rns.arborisis.com, section "LoRa"): a relay that differs on any
// single one of them never hears the network, and the network never hears
// it. 869.525 MHz sits in the 869.4–869.65 MHz sub-band (ETSI EN 300 220):
// 500 mW e.r.p., 10 % duty cycle, licence-free in the EU — which is why the
// long-term airtime limit below is 10 %, and why the radio keeps the RNode
// interference avoidance on (the upstream firmware disables it for US915,
// where there is no duty-cycle rule to share the band under).

#ifndef ARBORISIS_H
#define ARBORISIS_H

#ifdef ARBORISIS_RELAY

// Bump on every release published to rns.arborisis.com/relay; the
// configurator shows it, and manifest.json carries it next to the SHA-256.
#define ARBORISIS_RELAY_VERSION   "0.1.0"
#define ARBORISIS_RELAY_NAME      "Arborisis Relay"

// --- The channel ------------------------------------------------------------
#define ARBORISIS_LORA_FREQ_HZ    869525000UL
#define ARBORISIS_LORA_BW_HZ      125000UL
#define ARBORISIS_LORA_SF         8
#define ARBORISIS_LORA_CR         5      // 4/5
// 22 dBm is the SX1262 ceiling on the Heltec V3. With a 2–3 dBi whip that is
// ~200 mW e.r.p., under the 500 mW the sub-band allows. The V4 (PA, 28 dBm)
// is clamped to the same figure: more power on a shared band buys nothing
// once the other side cannot answer as loud.
#define ARBORISIS_LORA_TXP_DBM    22
// Airtime budget, in percent of a rolling window: 15 s (short) and 1 h
// (long). 0 disables. The long-term one is the legal ceiling of the sub-band.
#define ARBORISIS_ST_AIRTIME_PCT  0.0f
#define ARBORISIS_LT_AIRTIME_PCT  10.0f
#define ARBORISIS_AVOID_INTERFERENCE true

// --- The gateway ------------------------------------------------------------
// Backbone slot 1 on a fresh device. The configurator lets a relay operator
// add slots (a second gateway, a local rnsd) but never needs to type this one.
#define ARBORISIS_BACKBONE_HOST   "rns.arborisis.com"
#define ARBORISIS_BACKBONE_PORT   4242

// --- Names ------------------------------------------------------------------
// The open access point of the captive portal (the fallback for anyone
// without a Web Serial browser), the discovery name announced when the
// operator gave none, the mDNS hostname, and the OLED title.
#define ARBORISIS_AP_SSID         "Arborisis-Relay-Setup"
#define ARBORISIS_NAME_PREFIX     "Arborisis-"
#define ARBORISIS_MDNS_PREFIX     "arborisis-relay"
#define ARBORISIS_MDNS_KIND       "arborisis-relay"
#define ARBORISIS_DISPLAY_TITLE   "Arborisis"

// --- Advertisement ----------------------------------------------------------
// A relay exists to be found: the point of the network is coverage, and a
// pin on rmap.world is how another operator learns there is one to join.
// Still, the announce only carries a position once the operator typed one,
// and the ~0.5 km jitter is on unless they turn it off — a rooftop is a home.
#define ARBORISIS_ADVERT_DEFAULT  true
#define ARBORISIS_JITTER_DEFAULT  true

#endif // ARBORISIS_RELAY

// ─── Defaults seen by the rest of the firmware ──────────────────────────────
// Upstream values when this is not an Arborisis build. Keep every fallback
// identical to what the corresponding upstream code had inline.

#ifndef ARBORISIS_LORA_FREQ_HZ
#define ARBORISIS_LORA_FREQ_HZ    914875000UL
#endif
#ifndef ARBORISIS_LORA_BW_HZ
#define ARBORISIS_LORA_BW_HZ      125000UL
#endif
#ifndef ARBORISIS_LORA_SF
#define ARBORISIS_LORA_SF         10
#endif
#ifndef ARBORISIS_LORA_CR
#define ARBORISIS_LORA_CR         5
#endif
#ifndef ARBORISIS_LORA_TXP_DBM
#define ARBORISIS_LORA_TXP_DBM    28
#endif
#ifndef ARBORISIS_ST_AIRTIME_PCT
#define ARBORISIS_ST_AIRTIME_PCT  0.0f
#endif
#ifndef ARBORISIS_LT_AIRTIME_PCT
#define ARBORISIS_LT_AIRTIME_PCT  0.0f
#endif
#ifndef ARBORISIS_AVOID_INTERFERENCE
#define ARBORISIS_AVOID_INTERFERENCE false
#endif
#ifndef ARBORISIS_AP_SSID
#define ARBORISIS_AP_SSID         "RTNode-Setup"
#endif
#ifndef ARBORISIS_NAME_PREFIX
#define ARBORISIS_NAME_PREFIX     "RTNode-"
#endif
#ifndef ARBORISIS_MDNS_PREFIX
#define ARBORISIS_MDNS_PREFIX     "rtnode"
#endif
#ifndef ARBORISIS_MDNS_KIND
#define ARBORISIS_MDNS_KIND       "rtnode-heltec"
#endif
#ifndef ARBORISIS_DISPLAY_TITLE
#define ARBORISIS_DISPLAY_TITLE   "RTNode"
#endif
#ifndef ARBORISIS_ADVERT_DEFAULT
#define ARBORISIS_ADVERT_DEFAULT  false
#endif
#ifndef ARBORISIS_JITTER_DEFAULT
#define ARBORISIS_JITTER_DEFAULT  false
#endif

// The backbone defaults feed FirewallMode.h's own FIREWALL_* macros, which
// the upstream code already reads; only an Arborisis build overrides them.
#ifdef ARBORISIS_RELAY
  #ifndef FIREWALL_BACKBONE_HOST
  #define FIREWALL_BACKBONE_HOST  ARBORISIS_BACKBONE_HOST
  #endif
  #ifndef FIREWALL_BACKBONE_PORT
  #define FIREWALL_BACKBONE_PORT  ARBORISIS_BACKBONE_PORT
  #endif
#endif

#endif // ARBORISIS_H
