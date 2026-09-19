// Copyright (C) 2026, Arborisis
// Arborisis Relay — the OLED, as a relay operator reads it.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// RelayDisplay.h — the right-hand panel of the 128×64 OLED, in pages.
//
// The stock RTNode panel shows the channel and the IP address, and stops
// there: sixty-four pixels square is one screen of information, and it chose
// the one you need to confirm a flash went right. A relay on a rooftop is
// asked different questions — is anything coming through, from how far, is
// the gateway link up, what is this node called on the map — and answering
// them without a laptop is the point of having a screen at all.
//
// So the panel cycles through three pages, every RELAY_PAGE_MS or on a short
// press of the PRG button (which also unblanks the screen, as upstream):
//
//   1  RADIO    the channel, TX power, IP — the flash-went-right page
//   2  TRAFFIC  LoRa packets in/out, packets bridged each way, last RSSI/SNR
//   3  NODE     transport identity, uptime, heap, hour airtime, gateway links
//
// The left-hand panel (LORA / WIFI / WAN / LAN indicators, battery, signal)
// is upstream's and unchanged: it is the glanceable half, this is the one
// you stop and read. The title bar names the page and marks it with dots,
// so a photo of the screen says which page it shows.
//
// Org_01 is 6 px per capital or digit, 7 px tall: ten characters a line,
// six lines under the title bar. Every string below is written to fit that,
// and abbreviations were chosen over wrapping — "L>T" for LoRa-to-TCP is
// terse, but a line that wraps is unreadable.

#ifndef RELAY_DISPLAY_H
#define RELAY_DISPLAY_H

#if defined(FIREWALL_MODE) && defined(ARBORISIS_RELAY)

#include "Arborisis.h"
#include "FirewallMode.h"

#define RELAY_PAGES   3
#define RELAY_PAGE_MS 5000

static uint8_t  relay_page = 0;
static uint32_t relay_page_since = 0;

extern uint32_t rtc_node_hash_magic;
extern char     rtc_node_hash_hex[33];
extern IPAddress wr_device_ip;

inline void relay_display_next_page() {
    relay_page = (relay_page + 1) % RELAY_PAGES;
    relay_page_since = millis();
}

static void relay_display_tick() {
    if (relay_page_since == 0) relay_page_since = millis();
    if (millis() - relay_page_since >= RELAY_PAGE_MS) relay_display_next_page();
}

// y is the text baseline; the font sits 5 px above it.
static void relay_line(int y, const char* text) {
    disp_area.setCursor(3, y);
    disp_area.print(text);
}

static void relay_title(const char* title) {
    disp_area.fillRect(0, 0, disp_area.width(), 9, SSD1306_WHITE);
    disp_area.setTextColor(SSD1306_BLACK);
    disp_area.setCursor(3, 7);
    // Leave room for the page dots: 3 dots × 4 px, right-aligned.
    char buf[9];
    strncpy(buf, title, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    disp_area.print(buf);
    for (uint8_t i = 0; i < RELAY_PAGES; i++) {
        int x = disp_area.width() - 3 - (RELAY_PAGES - i) * 4;
        if (i == relay_page) disp_area.fillRect(x, 3, 3, 3, SSD1306_BLACK);
        else                 disp_area.drawPixel(x + 1, 4, SSD1306_BLACK);
    }
    disp_area.setTextColor(SSD1306_WHITE);
}

static void relay_format_uptime(char* out, size_t n) {
    uint32_t s = millis() / 1000;
    uint32_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
    if (d > 0)      snprintf(out, n, "Up %lud%02luh", (unsigned long)d, (unsigned long)h);
    else if (h > 0) snprintf(out, n, "Up %luh%02lum", (unsigned long)h, (unsigned long)m);
    else            snprintf(out, n, "Up %lum", (unsigned long)m);
}

static void relay_page_radio() {
    relay_title(firewall_state.node_name[0] ? firewall_state.node_name : ARBORISIS_DISPLAY_TITLE);
    char buf[16];
    if (!radio_online) {
        relay_line(17, "Radio OFF");
    } else {
        snprintf(buf, sizeof(buf), "%.3fMHz", (double)lora_freq / 1000000.0);
        relay_line(17, buf);
        snprintf(buf, sizeof(buf), "SF%d %luk", lora_sf, (unsigned long)(lora_bw / 1000));
        relay_line(25, buf);
        snprintf(buf, sizeof(buf), "4/%d %ddBm", lora_cr, lora_txp);
        relay_line(33, buf);
    }
    disp_area.drawLine(0, 38, disp_area.width() - 1, 38, SSD1306_WHITE);
    if (firewall_state.wifi_connected) {
        disp_area.setCursor(3, 47);
        disp_area.print(wr_device_ip);
    } else if (!firewall_state.wifi_enabled) {
        relay_line(47, "WiFi off");
    } else {
        relay_line(47, "No WiFi");
    }
    if (firewall_state.ap_tcp_enabled) {
        snprintf(buf, sizeof(buf), "LAN :%u", firewall_state.ap_tcp_port);
        relay_line(56, buf);
    } else if (radio_online) {
        // The hour's airtime against the budget — the number the duty-cycle
        // rule is about, and the one that explains a silent radio (lock).
        if (airtime_lock) snprintf(buf, sizeof(buf), "Air LOCK");
        else snprintf(buf, sizeof(buf), "Air %.1f%%", longterm_airtime * 100.0);
        relay_line(56, buf);
    }
}

static void relay_page_traffic() {
    relay_title("TRAFFIC");
    char buf[16];
    snprintf(buf, sizeof(buf), "RX %lu", (unsigned long)stat_rx);   relay_line(17, buf);
    snprintf(buf, sizeof(buf), "TX %lu", (unsigned long)stat_tx);   relay_line(25, buf);
    snprintf(buf, sizeof(buf), "L>T %lu", (unsigned long)firewall_state.packets_bridged_lora_to_tcp); relay_line(33, buf);
    snprintf(buf, sizeof(buf), "T>L %lu", (unsigned long)firewall_state.packets_bridged_tcp_to_lora); relay_line(41, buf);
    if (last_rssi > -292) {
        // The SX126x reports SNR in quarter-dB, two's complement.
        snprintf(buf, sizeof(buf), "RSSI %d", last_rssi);           relay_line(49, buf);
        snprintf(buf, sizeof(buf), "SNR %.1f", ((int8_t)last_snr_raw) / 4.0); relay_line(57, buf);
    } else {
        relay_line(49, "No RX yet");
        if (noise_floor > -292) { snprintf(buf, sizeof(buf), "Floor %d", noise_floor); relay_line(57, buf); }
    }
}

static void relay_page_node() {
    relay_title("NODE");
    char buf[16];
    // Sixteen of the thirty-two hex digits, on two lines: enough to find the
    // pin on rmap.world, and what `rnpath -t` shows at the gateway.
    if (rtc_node_hash_magic == 0x504B4841UL && rtc_node_hash_hex[0] != '\0') {
        snprintf(buf, sizeof(buf), "ID %.8s", rtc_node_hash_hex);      relay_line(17, buf);
        snprintf(buf, sizeof(buf), "   %.8s", rtc_node_hash_hex + 8);  relay_line(25, buf);
    } else {
        relay_line(17, "ID pending");
    }
    relay_format_uptime(buf, sizeof(buf));                             relay_line(33, buf);
    snprintf(buf, sizeof(buf), "Heap %luk", (unsigned long)(ESP.getFreeHeap() / 1024)); relay_line(41, buf);
    size_t wan_en = firewall_backbone_enabled_count();
    size_t wan_up = firewall_backbone_connected_count();
    if (wan_en == 0) snprintf(buf, sizeof(buf), "GW none");
    else             snprintf(buf, sizeof(buf), "GW %u/%u %s", (unsigned)wan_up, (unsigned)wan_en, wan_up ? "up" : "dn");
    relay_line(49, buf);
    if (firewall_state.advert_enabled) relay_line(57, "On the map");
    else                               relay_line(57, "Unlisted");
}

// Replaces the FIREWALL_MODE branch of draw_disp_area() for this build.
static void relay_draw_disp_area() {
    relay_display_tick();
    disp_area.fillRect(0, 0, disp_area.width(), disp_area.height(), SSD1306_BLACK);
    disp_area.setFont(SMALL_FONT);
    disp_area.setTextWrap(false);
    disp_area.setTextSize(1);
    disp_area.setTextColor(SSD1306_WHITE);
    switch (relay_page) {
        case 1:  relay_page_traffic(); break;
        case 2:  relay_page_node();    break;
        default: relay_page_radio();   break;
    }
    // Bottom divider (maps to y=127 at 2x display scale), as upstream.
    disp_area.drawLine(0, 63, disp_area.width() - 1, 63, SSD1306_WHITE);
}

#endif // FIREWALL_MODE && ARBORISIS_RELAY
#endif // RELAY_DISPLAY_H
