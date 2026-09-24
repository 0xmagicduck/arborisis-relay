// Copyright (C) 2026, Arborisis
// Arborisis Pocket — the Wio Tracker L1 Pro as an RNode of the network.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// ArborisisPocket.h — what a fresh Wio Tracker L1 Pro does at its first boot.
//
// A stock RNode is provisioned twice after flashing: `rnodeconf --rom`
// writes the product, model and revision codes into the EEPROM and locks
// them, `rnodeconf --firmware-hash` records the hash of the image that was
// just flashed. Until both are done, the firmware calls itself
// unprovisioned and starts nothing — the right answer for a device that a
// shop ships, and the wrong one for a device that someone flashed by
// dragging a UF2 onto a USB drive, which is how a Wio Tracker takes a
// firmware and how the page at rns.arborisis.com/relay will hand this one
// out. There is no rnodeconf on that road, and there is nobody to explain
// why the screen shows a question mark.
//
// So the pocket provisions itself. At first boot, when the EEPROM (a file
// on the nRF52's internal flash, see Utilities.h) is still unlocked, it
// writes what rnodeconf would have — the codes of Boards.h, a serial from
// the chip's own ID, the release date, the checksum — and locks. The
// firmware hash is not recorded: the build turns VALIDATE_FIRMWARE off
// instead (platformio.ini), which is what the hash check would have proven
// anyway, that the image in flash is the one that was flashed.
//
// Then the channel. A stock RNode waits for its host to set a frequency;
// this one comes up on the network's channel with the transport enabled,
// like the relay, so that a device left on a windowsill with nothing
// attached repeats for the phones around it. A host (Sideband, over BLE or
// USB) can still change every parameter, and `rnodeconf --eeprom-wipe`
// still returns the device to this very first boot.
//
// Everything here runs once per EEPROM, before the firmware validates it,
// and touches nothing that is already set.

#ifndef ARBORISIS_POCKET_H
#define ARBORISIS_POCKET_H

#ifdef ARBORISIS_POCKET

#include "Arborisis.h"

#if MCU_VARIANT != MCU_NRF52
#error "ARBORISIS_POCKET is an nRF52 profile (the Seeed Wio Tracker L1)"
#endif

// The transport identity, for the NODE page (RelayDisplay.h): 32 hex digits,
// empty until RNS has started. The relay keeps the same thing in RTC memory
// so the captive portal can show it; the pocket has no portal, and no RTC.
char pocket_node_hash_hex[33] = "";

// The ROM: product, model, revision, serial, date, checksum, lock — the
// bytes `rnodeconf --rom` writes, in the order ROM.h lays them out.
static bool pocket_provision_rom() {
    if (eeprom_lock_set()) return false;

    eeprom_update(eeprom_addr(ADDR_PRODUCT), PRODUCT_WIO_TRACKER_L1);
    eeprom_update(eeprom_addr(ADDR_MODEL), MODEL_1A);
    eeprom_update(eeprom_addr(ADDR_HW_REV), ARBORISIS_POCKET_HWREV);

    // The serial: the low word of the nRF52840's factory device ID, which
    // is unique per chip and what the BLE address derives from.
    uint32_t serial = NRF_FICR->DEVICEID[0];
    uint32_t made = ARBORISIS_POCKET_MADE;
    for (int i = 0; i < 4; i++) {
        eeprom_update(eeprom_addr(ADDR_SERIAL + i), (uint8_t)(serial >> (24 - 8 * i)));
        eeprom_update(eeprom_addr(ADDR_MADE + i), (uint8_t)(made >> (24 - 8 * i)));
    }

    // MD5 over the eleven bytes above, as eeprom_checksum_valid() checks it.
    char data[CHECKSUMMED_SIZE];
    for (uint8_t i = 0; i < CHECKSUMMED_SIZE; i++) data[i] = (char)eeprom_read(eeprom_addr(i));
    unsigned char* hash = MD5::make_hash(data, CHECKSUMMED_SIZE);
    for (uint8_t i = 0; i < 16; i++) eeprom_update(eeprom_addr(ADDR_CHKSUM + i), hash[i]);
    free(hash);

    // The device signature (ADDR_SIGNATURE) stays blank: it is the
    // manufacturer's Ed25519 signature over the ROM, and the key is not
    // ours. Nothing that runs depends on it; `rnodeconf -i` says unsigned.

    // Bluetooth on from the first boot — the phone is the point — and the
    // RNode's interference avoidance on, as the profile has it: the EU
    // sub-band is shared under a duty-cycle rule. Pairing still takes the
    // long press of the button, as on every RNode.
    eeprom_update(eeprom_addr(ADDR_CONF_BT), BT_ENABLE_BYTE);
    eeprom_update(eeprom_addr(ADDR_CONF_DIA), ARBORISIS_AVOID_INTERFERENCE ? 0x00 : 0x01);

    // The lock, last: a boot that dies halfway through the writes above
    // finds an unlocked EEPROM next time and writes them again.
    eeprom_update(eeprom_addr(ADDR_INFO_LOCK), INFO_LOCK_BYTE);
    eeprom_flush();
    return true;
}

// The channel, unless a host or an operator has already stored one. Written
// the way eeprom_conf_save() writes it, without its hw_ready gate: this
// runs before the radio is validated, on purpose, so that validation finds
// a configuration and boots the transport.
static bool pocket_preset_channel() {
    if (eeprom_have_conf()) return false;

    uint32_t bw = ARBORISIS_LORA_BW_HZ, freq = ARBORISIS_LORA_FREQ_HZ;
    eeprom_update(eeprom_addr(ADDR_CONF_SF), ARBORISIS_LORA_SF);
    eeprom_update(eeprom_addr(ADDR_CONF_CR), ARBORISIS_LORA_CR);
    eeprom_update(eeprom_addr(ADDR_CONF_TXP), ARBORISIS_LORA_TXP_DBM);
    for (int i = 0; i < 4; i++) {
        eeprom_update(eeprom_addr(ADDR_CONF_BW) + i, (uint8_t)(bw >> (24 - 8 * i)));
        eeprom_update(eeprom_addr(ADDR_CONF_FREQ) + i, (uint8_t)(freq >> (24 - 8 * i)));
    }
    eeprom_update(eeprom_addr(ADDR_CONF_OK), CONF_OK_BYTE);
    eeprom_flush();
    return true;
}

// Called once from setup(), after the EEPROM file is open and before
// validate_status() reads it.
inline void pocket_provision_if_needed() {
    bool rom = pocket_provision_rom();
    bool channel = pocket_preset_channel();
    if (rom) Serial.write("[Pocket] Fresh device: ROM provisioned (" ARBORISIS_POCKET_NAME ")\r\n");
    if (channel) {
        Serial.printf("[Pocket] Fresh device: channel preset to %lu Hz, %lu Hz, SF%d, CR4/%d, %d dBm\r\n",
                      (unsigned long)ARBORISIS_LORA_FREQ_HZ, (unsigned long)ARBORISIS_LORA_BW_HZ,
                      ARBORISIS_LORA_SF, ARBORISIS_LORA_CR, ARBORISIS_LORA_TXP_DBM);
    }
}

#endif // ARBORISIS_POCKET
#endif // ARBORISIS_POCKET_H
