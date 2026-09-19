// Copyright (C) 2026, Arborisis
// Arborisis Relay — serial configuration protocol.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// SerialConfig.h — configure the relay over the USB serial port, in JSON.
//
// RTNode configures itself through a captive portal: join the device's own
// access point, open 10.0.0.1, fill a form. That works, and it stays as the
// fallback, but it cannot be driven by a web page — and the page at
// rns.arborisis.com/relay is where a new relay operator already is, with the
// board plugged in over USB, having just flashed it from the same tab. Web
// Serial gives that tab the port; this file gives the port a language.
//
// The wire format is one JSON object per line, each line prefixed with
// "ARB " in both directions:
//
//   ARB {"cmd":"hello"}                → identity, defaults, config, status
//   ARB {"cmd":"get"}                  → config + status
//   ARB {"cmd":"set", ...fields...}    → validates, saves, {"ok":true}, reboots
//   ARB {"cmd":"reboot"}
//
// Why a prefix and not bare JSON: the same serial line carries the RNode
// KISS protocol (binary frames delimited by 0xC0) and a stream of free-text
// diagnostics ("[Boundary] ..."). The firmware only ever parses a line that
// starts with the prefix, so no log line, and no byte of a KISS frame, can be
// mistaken for a command; the browser filters replies the same way. Bytes
// inside a KISS frame never reach this parser (see serial_callback), and a
// JSON line never contains 0xC0, so the two protocols cannot corrupt each
// other.
//
// Two entry points feed the parser, because the firmware has two lives:
//   - in the captive-portal loop, setup() blocks and reads Serial directly
//     (serial_config_poll);
//   - in normal operation, the KISS machinery drains Serial into a FIFO and
//     serial_callback hands us every byte that is outside a frame
//     (serial_config_feed).
//
// A `set` writes exactly what the portal's /save would write, through the
// same two functions (firewall_save_config, firewall_save_radio_config), so
// a device configured from the page and one configured from the portal are
// indistinguishable on the next boot. Any field left out keeps its value.

#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#ifdef FIREWALL_MODE

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "Arborisis.h"
#include "FirewallMode.h"

// Generous for a `set` that carries four backbone slots and a 32-char
// passphrase, small enough not to matter on a board without PSRAM: the
// parser only holds one line at a time and frees it when the reply is out.
#define SERIAL_CONFIG_LINE_MAX 1536
#define SERIAL_CONFIG_PREFIX   "ARB "

// These live in RNode_Firmware.ino (RTC memory: the transport identity hash,
// cached so the portal can show it without RNS running). Declared here
// because this header is included before them in the .ino would be the
// natural order, but they are definitions the .ino owns.
extern uint32_t rtc_node_hash_magic;
extern char     rtc_node_hash_hex[33];
#define SERIAL_CONFIG_NODE_HASH_MAGIC 0x504B4841UL

extern bool wifi_is_connected();
bool config_portal_is_active();

static char     serial_config_line[SERIAL_CONFIG_LINE_MAX];
static size_t   serial_config_len = 0;
static bool     serial_config_overflow = false;
// A `set` answers first and reboots a moment later, so the browser gets the
// acknowledgement before the port goes away.
static uint32_t serial_config_reboot_at = 0;

static const char* serial_config_board() {
#if BOARD_MODEL == BOARD_HELTEC32_V3
    return "heltec_v3";
#elif BOARD_MODEL == BOARD_HELTEC32_V4
    return "heltec_v4";
#else
    return "unknown";
#endif
}

static void serial_config_send(JsonDocument& doc) {
    Serial.print(SERIAL_CONFIG_PREFIX);
    serializeJson(doc, Serial);
    Serial.print("\r\n");
    Serial.flush();
}

static void serial_config_error(const char* code, const char* detail = nullptr) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["type"] = "error";
    doc["error"] = code;
    if (detail) doc["detail"] = detail;
    serial_config_send(doc);
}

// ─── Reading the configuration ───────────────────────────────────────────────

static void serial_config_read_eeprom_string(int addr, char* out, size_t out_len) {
    // 0xFF is the erased state of the EEPROM and never a legitimate
    // character here; the portal writes 0x00 padding, older firmware left 0xFF.
    size_t i = 0;
    for (; i + 1 < out_len; i++) {
        uint8_t c = EEPROM.read(config_addr(addr + i));
        if (c == 0x00 || c == 0xFF) break;
        out[i] = (char)c;
    }
    out[i] = '\0';
}

static void serial_config_fill_config(JsonObject cfg) {
    char buf[33];

    cfg["name"] = firewall_state.node_name;

    JsonObject wifi = cfg["wifi"].to<JsonObject>();
    wifi["enabled"] = firewall_state.wifi_enabled;
    serial_config_read_eeprom_string(ADDR_CONF_SSID, buf, sizeof(buf));
    wifi["ssid"] = buf;
    // The passphrase is never read back: the page has no reason to see it,
    // and a `get` is the one message a bystander with a terminal could send.
    serial_config_read_eeprom_string(ADDR_CONF_PSK, buf, sizeof(buf));
    wifi["psk_set"] = (buf[0] != '\0');

    JsonArray bbs = cfg["backbones"].to<JsonArray>();
    for (size_t i = 0; i < FIREWALL_BACKBONE_SLOTS; i++) {
        JsonObject bb = bbs.add<JsonObject>();
        bb["enabled"] = firewall_state.backbones[i].enabled;
        bb["host"] = firewall_state.backbones[i].host;
        bb["port"] = firewall_state.backbones[i].port;
    }

    JsonObject lan = cfg["lan"].to<JsonObject>();
    lan["enabled"] = firewall_state.ap_tcp_enabled;
    lan["port"] = firewall_state.ap_tcp_port;

    JsonObject ifac = cfg["ifac"].to<JsonObject>();
    ifac["enabled"] = firewall_state.ifac_enabled;
    ifac["name"] = firewall_state.ifac_netname;
    ifac["pass_set"] = (firewall_state.ifac_passphrase[0] != '\0');

    JsonObject adv = cfg["advertise"].to<JsonObject>();
    adv["enabled"] = firewall_state.advert_enabled;
    if (firewall_state.advert_lat != 0.0 || firewall_state.advert_lon != 0.0) {
        adv["lat"] = firewall_state.advert_lat;
        adv["lon"] = firewall_state.advert_lon;
    } else {
        adv["lat"] = nullptr;
        adv["lon"] = nullptr;
    }
    adv["jitter"] = firewall_state.advert_jitter;

    JsonObject radio = cfg["radio"].to<JsonObject>();
    radio["frequency_hz"] = lora_freq;
    radio["bandwidth_hz"] = lora_bw;
    radio["spreading_factor"] = lora_sf;
    radio["coding_rate"] = lora_cr;
    radio["txpower_dbm"] = lora_txp;
    radio["airtime_short_pct"] = firewall_state.st_airtime_limit * 100.0f;
    radio["airtime_long_pct"] = firewall_state.lt_airtime_limit * 100.0f;

    JsonObject mdns = cfg["mdns"].to<JsonObject>();
    mdns["enabled"] = firewall_state.mdns_enabled;
    mdns["hostname"] = firewall_state.mdns_hostname;

    cfg["probe"] = firewall_state.probe_enabled;
}

static void serial_config_fill_status(JsonObject st) {
    st["mode"] = config_portal_is_active() ? "portal" : "run";
    st["uptime_s"] = millis() / 1000;
    st["free_heap"] = ESP.getFreeHeap();
    st["radio_online"] = radio_online;
    st["wifi_connected"] = firewall_state.wifi_connected;
    if (firewall_state.wifi_connected) st["ip"] = WiFi.localIP().toString();
    else st["ip"] = nullptr;

    JsonArray bbs = st["backbones"].to<JsonArray>();
    for (size_t i = 0; i < FIREWALL_BACKBONE_SLOTS; i++) {
        bbs.add(firewall_state.backbones[i].enabled && firewall_state.backbones[i].connected);
    }
    st["lan_client"] = firewall_state.ap_tcp_connected;

    // The transport identity: what rmap.world lists, what `rnpath -t` shows
    // at the gateway. Known only after RNS has started once on this flash.
    if (rtc_node_hash_magic == SERIAL_CONFIG_NODE_HASH_MAGIC && rtc_node_hash_hex[0] != '\0') {
        st["transport_id"] = rtc_node_hash_hex;
    } else {
        st["transport_id"] = nullptr;
    }

    st["airtime_pct"] = airtime * 100.0f;
    st["airtime_long_pct"] = longterm_airtime * 100.0f;
    st["airtime_lock"] = airtime_lock;
    st["last_rssi"] = last_rssi;
    // The SX126x reports SNR in quarter-dB, two's complement.
    st["last_snr"] = ((int8_t)last_snr_raw) / 4.0f;
    st["noise_floor"] = noise_floor;
    st["rx_packets"] = stat_rx;
    st["tx_packets"] = stat_tx;
    st["bridged_lora_to_tcp"] = firewall_state.packets_bridged_lora_to_tcp;
    st["bridged_tcp_to_lora"] = firewall_state.packets_bridged_tcp_to_lora;
}

static void serial_config_reply_state(const char* type, bool with_identity) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["type"] = type;
    if (with_identity) {
        doc["fw"] = "arborisis-relay";
#ifdef ARBORISIS_RELAY
        doc["version"] = ARBORISIS_RELAY_VERSION;
        doc["profile"] = "arborisis";
#else
        doc["version"] = FW_RELEASE_TAG;
        doc["profile"] = "rtnode";
#endif
        doc["base"] = "rtnode " FW_RELEASE_TAG;
        doc["board"] = serial_config_board();
        // What the page should preselect: the network's channel, and the
        // profile's stance on being listed. The device itself never turns
        // advertising on without a position (see FirewallMode.h).
        JsonObject def = doc["defaults"].to<JsonObject>();
        def["frequency_hz"] = (uint32_t)ARBORISIS_LORA_FREQ_HZ;
        def["bandwidth_hz"] = (uint32_t)ARBORISIS_LORA_BW_HZ;
        def["spreading_factor"] = ARBORISIS_LORA_SF;
        def["coding_rate"] = ARBORISIS_LORA_CR;
        def["txpower_dbm"] = ARBORISIS_LORA_TXP_DBM;
        def["airtime_long_pct"] = ARBORISIS_LT_AIRTIME_PCT;
        def["backbone_host"] = FIREWALL_BACKBONE_HOST;
        def["backbone_port"] = FIREWALL_BACKBONE_PORT;
        def["advertise"] = ARBORISIS_ADVERT_DEFAULT;
        def["jitter"] = ARBORISIS_JITTER_DEFAULT;
    }
    serial_config_fill_config(doc["config"].to<JsonObject>());
    serial_config_fill_status(doc["status"].to<JsonObject>());
    serial_config_send(doc);
}

// ─── Writing the configuration ───────────────────────────────────────────────

static void serial_config_copy_str(char* dst, size_t dst_len, JsonVariantConst v) {
    const char* s = v.as<const char*>();
    memset(dst, 0, dst_len);
    if (s) strncpy(dst, s, dst_len - 1);
}

static void serial_config_write_eeprom_string(int addr, const char* s, size_t field_len) {
    // field_len is the size with terminator (33 for a 32-char field): the
    // portal pads with 0x00 and terminates at [field_len - 1]; same here.
    size_t n = strlen(s);
    for (size_t i = 0; i + 1 < field_len; i++) {
        EEPROM.write(config_addr(addr + i), i < n ? (uint8_t)s[i] : 0x00);
    }
    EEPROM.write(config_addr(addr + field_len - 1), 0x00);
}

// Present in the object, even as null — `isNull()` alone cannot tell "absent"
// from "cleared", and the coordinates need that distinction.
static bool json_has(JsonObjectConst o, const char* key) {
    for (JsonPairConst kv : o) if (strcmp(kv.key().c_str(), key) == 0) return true;
    return false;
}

// A string of at most `max` characters; anything else (a number, an object,
// too long) is a refusal, never a crash on a null pointer.
static bool json_str_ok(JsonVariantConst v, size_t max) {
    if (v.isNull()) return true;
    const char* s = v.as<const char*>();
    return s != nullptr && strlen(s) <= max;
}

// Returns nullptr on success, or the name of the field that was refused.
// Refusing beats coercing: the portal silently turns a bad latitude into
// 0.0, which is exactly how a pin ends up in the Atlantic. The page shows
// the field name and nothing is written.
static const char* serial_config_apply(JsonObjectConst in) {
    // ── validate everything first, write nothing ──
    if (!json_str_ok(in["name"], 32)) return "name";

    JsonObjectConst wifi = in["wifi"];
    if (!wifi.isNull()) {
        if (!json_str_ok(wifi["ssid"], 32)) return "wifi.ssid";
        if (!json_str_ok(wifi["psk"], 32)) return "wifi.psk";
        if (!wifi["psk"].isNull()) {
            // WPA2 wants 8 to 63; the EEPROM field stops at 32. Empty = open network.
            size_t n = strlen(wifi["psk"].as<const char*>());
            if (n > 0 && n < 8) return "wifi.psk";
        }
    }

    JsonArrayConst bbs = in["backbones"];
    if (!bbs.isNull()) {
        if (bbs.size() > FIREWALL_BACKBONE_SLOTS) return "backbones";
        for (JsonObjectConst bb : bbs) {
            if (!json_str_ok(bb["host"], FIREWALL_BACKBONE_HOST_LEN - 1)) return "backbones.host";
            if (!bb["port"].isNull()) {
                long p = bb["port"].as<long>();
                if (p < 1 || p > 65535) return "backbones.port";
            }
        }
    }

    JsonObjectConst lan = in["lan"];
    if (!lan.isNull() && !lan["port"].isNull()) {
        long p = lan["port"].as<long>();
        if (p < 1 || p > 65535) return "lan.port";
    }

    JsonObjectConst ifac = in["ifac"];
    if (!ifac.isNull()) {
        if (!json_str_ok(ifac["name"], 32)) return "ifac.name";
        if (!json_str_ok(ifac["pass"], 32)) return "ifac.pass";
    }

    JsonObjectConst adv = in["advertise"];
    double new_lat = firewall_state.advert_lat, new_lon = firewall_state.advert_lon;
    bool new_advert = firewall_state.advert_enabled;
    if (!adv.isNull()) {
        // lat/lon: absent = keep, null = clear, number = set (both or none)
        bool has_lat = json_has(adv, "lat"), has_lon = json_has(adv, "lon");
        if (has_lat != has_lon) return "advertise.lat";
        if (has_lat) {
            if (adv["lat"].isNull()) { new_lat = 0.0; new_lon = 0.0; }
            else {
                if (!adv["lat"].is<double>() || !adv["lon"].is<double>()) return "advertise.lat";
                new_lat = adv["lat"].as<double>(); new_lon = adv["lon"].as<double>();
                if (isnan(new_lat) || isnan(new_lon) || new_lat < -90.0 || new_lat > 90.0 ||
                    new_lon < -180.0 || new_lon > 180.0) return "advertise.lat";
            }
        }
        if (!adv["enabled"].isNull()) new_advert = adv["enabled"].as<bool>();
    }
    // The announce always carries a position: advertising without one is
    // refused rather than quietly pinning 0,0.
    if (new_advert && new_lat == 0.0 && new_lon == 0.0) return "advertise.enabled";

    JsonObjectConst radio = in["radio"];
    if (!radio.isNull()) {
        if (!radio["frequency_hz"].isNull()) {
            uint32_t f = radio["frequency_hz"].as<uint32_t>();
            // The modem's own limits; the page enforces the band.
            if (f < 137000000UL || f > 1020000000UL) return "radio.frequency_hz";
        }
        if (!radio["bandwidth_hz"].isNull()) {
            uint32_t bw = radio["bandwidth_hz"].as<uint32_t>();
            if (bw < 7800UL || bw > 500000UL) return "radio.bandwidth_hz";
        }
        if (!radio["spreading_factor"].isNull()) {
            int sf = radio["spreading_factor"].as<int>();
            if (sf < 5 || sf > 12) return "radio.spreading_factor";
        }
        if (!radio["coding_rate"].isNull()) {
            int cr = radio["coding_rate"].as<int>();
            if (cr < 5 || cr > 8) return "radio.coding_rate";
        }
        if (!radio["txpower_dbm"].isNull()) {
            int txp = radio["txpower_dbm"].as<int>();
            // The V3's SX1262 stops at 22; the V4's PA goes to 28 but the
            // profile clamps it (Arborisis.h). Refuse rather than clamp, so
            // the page learns the ceiling instead of showing a number the
            // radio is not emitting.
            int ceiling = 28;
#if BOARD_MODEL == BOARD_HELTEC32_V3
            ceiling = 22;
#endif
#ifdef ARBORISIS_RELAY
            if (ceiling > ARBORISIS_LORA_TXP_DBM) ceiling = ARBORISIS_LORA_TXP_DBM;
#endif
            if (txp < 2 || txp > ceiling) return "radio.txpower_dbm";
        }
        for (const char* k : {"airtime_short_pct", "airtime_long_pct"}) {
            if (!radio[k].isNull()) {
                float pct = radio[k].as<float>();
                if (isnan(pct) || pct < 0.0f || pct > 25.0f) return k;
            }
        }
    }

    JsonObjectConst mdns = in["mdns"];
    if (!mdns.isNull() && !mdns["hostname"].isNull()) {
        if (!json_str_ok(mdns["hostname"], 32)) return "mdns.hostname";
        const char* h = mdns["hostname"].as<const char*>();
        size_t n = strlen(h);
        for (size_t i = 0; i < n; i++) {
            char c = h[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            if (!ok || (c == '-' && (i == 0 || i == n - 1))) return "mdns.hostname";
        }
    }

    // ── apply ──
    if (!in["name"].isNull()) serial_config_copy_str(firewall_state.node_name, sizeof(firewall_state.node_name), in["name"]);

    if (!wifi.isNull()) {
        if (!wifi["enabled"].isNull()) firewall_state.wifi_enabled = wifi["enabled"].as<bool>();
        if (!wifi["ssid"].isNull()) serial_config_write_eeprom_string(ADDR_CONF_SSID, wifi["ssid"].as<const char*>(), 33);
        if (!wifi["psk"].isNull())  serial_config_write_eeprom_string(ADDR_CONF_PSK,  wifi["psk"].as<const char*>(), 33);
        if (!wifi["ssid"].isNull() || !wifi["psk"].isNull()) {
            // Same as the portal: station mode, DHCP, no stale static address.
            EEPROM.write(eeprom_addr(ADDR_CONF_WIFI), WR_WIFI_STA);
            for (int i = 0; i < 4; i++) {
                EEPROM.write(config_addr(ADDR_CONF_IP + i), 0x00);
                EEPROM.write(config_addr(ADDR_CONF_NM + i), 0x00);
            }
        }
    }

    if (!bbs.isNull()) {
        // The array replaces the slots it covers; the rest are cleared, so
        // that the page's list is the device's list.
        size_t i = 0;
        for (JsonObjectConst bb : bbs) {
            FirewallBackboneSlot& slot = firewall_state.backbones[i++];
            serial_config_copy_str(slot.host, sizeof(slot.host), bb["host"]);
            slot.port = bb["port"].isNull() ? FIREWALL_BACKBONE_PORT : (uint16_t)bb["port"].as<long>();
            slot.enabled = bb["enabled"].isNull() ? (slot.host[0] != '\0') : bb["enabled"].as<bool>();
            if (slot.host[0] == '\0') slot.enabled = false;
        }
        for (; i < FIREWALL_BACKBONE_SLOTS; i++) {
            firewall_state.backbones[i].enabled = false;
            firewall_state.backbones[i].host[0] = '\0';
            firewall_state.backbones[i].port = FIREWALL_BACKBONE_PORT;
        }
    }

    if (!lan.isNull()) {
        if (!lan["enabled"].isNull()) firewall_state.ap_tcp_enabled = lan["enabled"].as<bool>();
        if (!lan["port"].isNull()) firewall_state.ap_tcp_port = (uint16_t)lan["port"].as<long>();
    }

    if (!ifac.isNull()) {
        if (!ifac["enabled"].isNull()) firewall_state.ifac_enabled = ifac["enabled"].as<bool>();
        if (!ifac["name"].isNull()) serial_config_copy_str(firewall_state.ifac_netname, sizeof(firewall_state.ifac_netname), ifac["name"]);
        if (!ifac["pass"].isNull()) serial_config_copy_str(firewall_state.ifac_passphrase, sizeof(firewall_state.ifac_passphrase), ifac["pass"]);
        if (firewall_state.ifac_enabled && firewall_state.ifac_netname[0] == '\0' && firewall_state.ifac_passphrase[0] == '\0') {
            firewall_state.ifac_enabled = false;
        }
    }

    firewall_state.advert_lat = new_lat;
    firewall_state.advert_lon = new_lon;
    firewall_state.advert_enabled = new_advert;
    if (!adv.isNull() && !adv["jitter"].isNull()) firewall_state.advert_jitter = adv["jitter"].as<bool>();

    if (!radio.isNull()) {
        if (!radio["frequency_hz"].isNull()) lora_freq = radio["frequency_hz"].as<uint32_t>();
        if (!radio["bandwidth_hz"].isNull()) lora_bw = radio["bandwidth_hz"].as<uint32_t>();
        if (!radio["spreading_factor"].isNull()) lora_sf = radio["spreading_factor"].as<int>();
        if (!radio["coding_rate"].isNull()) lora_cr = radio["coding_rate"].as<int>();
        if (!radio["txpower_dbm"].isNull()) lora_txp = radio["txpower_dbm"].as<int>();
        if (!radio["airtime_short_pct"].isNull()) firewall_state.st_airtime_limit = radio["airtime_short_pct"].as<float>() / 100.0f;
        if (!radio["airtime_long_pct"].isNull())  firewall_state.lt_airtime_limit = radio["airtime_long_pct"].as<float>() / 100.0f;
    }

    if (!mdns.isNull()) {
        if (!mdns["enabled"].isNull()) firewall_state.mdns_enabled = mdns["enabled"].as<bool>();
        if (!mdns["hostname"].isNull()) serial_config_copy_str(firewall_state.mdns_hostname, sizeof(firewall_state.mdns_hostname), mdns["hostname"]);
    }

    if (!in["probe"].isNull()) firewall_state.probe_enabled = in["probe"].as<bool>();

    // Both writers commit; the order is the portal's.
    firewall_save_config();
    firewall_save_radio_config();
    return nullptr;
}

// ─── The line parser ─────────────────────────────────────────────────────────

static void serial_config_handle_line(const char* line) {
    const size_t plen = strlen(SERIAL_CONFIG_PREFIX);
    if (strncmp(line, SERIAL_CONFIG_PREFIX, plen) != 0) return;
    line += plen;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) { serial_config_error("bad_json", err.c_str()); return; }

    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "hello") == 0) {
        serial_config_reply_state("hello", true);
    } else if (strcmp(cmd, "get") == 0) {
        serial_config_reply_state("state", false);
    } else if (strcmp(cmd, "set") == 0) {
        const char* refused = serial_config_apply(doc.as<JsonObjectConst>());
        if (refused) { serial_config_error("invalid", refused); return; }
        JsonDocument reply;
        reply["ok"] = true;
        reply["type"] = "saved";
        reply["reboot"] = true;
        serial_config_send(reply);
        serial_config_reboot_at = millis() + 600;
    } else if (strcmp(cmd, "reboot") == 0) {
        JsonDocument reply;
        reply["ok"] = true;
        reply["type"] = "rebooting";
        serial_config_send(reply);
        serial_config_reboot_at = millis() + 300;
    } else {
        serial_config_error("unknown_cmd", cmd);
    }
}

// One byte from outside any KISS frame.
inline void serial_config_feed(uint8_t c) {
    if (c == '\n' || c == '\r') {
        if (serial_config_len > 0 && !serial_config_overflow) {
            serial_config_line[serial_config_len] = '\0';
            serial_config_handle_line(serial_config_line);
        } else if (serial_config_overflow) {
            serial_config_error("line_too_long");
        }
        serial_config_len = 0;
        serial_config_overflow = false;
        return;
    }
    if (serial_config_overflow) return;
    if (serial_config_len + 1 >= SERIAL_CONFIG_LINE_MAX) { serial_config_overflow = true; return; }
    // Text only. A stray binary byte that is not a frame delimiter (a
    // KISS escape at the very start of a stream, say) is dropped, so it
    // cannot poison the line that follows.
    if (c < 0x20 || c > 0x7E) return;
    serial_config_line[serial_config_len++] = (char)c;
}

// For the captive-portal loop, where nothing else reads Serial.
inline void serial_config_poll() {
    int n = 0;
    while (Serial.available() && n++ < 256) serial_config_feed((uint8_t)Serial.read());
}

// Called from both loops: performs the deferred reboot once the reply is out.
inline void serial_config_housekeeping() {
    if (serial_config_reboot_at != 0 && (int32_t)(millis() - serial_config_reboot_at) >= 0) {
        Serial.println("[Config] Rebooting to apply the configuration received over serial");
        Serial.flush();
        delay(50);
        ESP.restart();
    }
}

#endif // FIREWALL_MODE
#endif // SERIAL_CONFIG_H
