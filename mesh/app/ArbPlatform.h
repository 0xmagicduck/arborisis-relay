// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// ArbPlatform.h — what differs between the MCU families, in one place: the
// file system everything is stored on, and the bytes the RNode protocol
// uses to name the platform.

#pragma once

#include <Arduino.h>
#include "RNodeHost.h"

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #define ARB_FS        InternalFS
  #define ARB_FS_TYPE   Adafruit_LittleFS
  #define ARB_ID_DIR    ""
  using namespace Adafruit_LittleFS_Namespace;
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  #define ARB_FS        LittleFS
  #define ARB_FS_TYPE   fs::FS
  #define ARB_ID_DIR    "/identity"
#elif defined(ESP32)
  // LittleFS rather than MeshCore's SPIFFS: Reticulum keeps directories
  // (its cache), which SPIFFS does not have. Both live in the same
  // "spiffs" partition, so the flash layout is MeshCore's.
  #include <LittleFS.h>
  #define ARB_FS        LittleFS
  #define ARB_FS_TYPE   fs::FS
  #define ARB_ID_DIR    "/identity"
#else
  #error "Arborisis Mesh: unsupported platform"
#endif

#ifndef ARB_WITH_RNS
  #define ARB_WITH_RNS 0
#endif

#ifndef ARB_VERSION
  #define ARB_VERSION "0.0.0-dev"
#endif

#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 20
#endif

namespace arb {

inline bool fsBegin() {
#if defined(ESP32)
  return ARB_FS.begin(true);
#elif defined(RP2040_PLATFORM)
  return ARB_FS.begin();
#else
  return ARB_FS.begin();
#endif
}

inline bool fsFormat() {
  return ARB_FS.format();
}

inline RNodeIdentity rnodeIdentity() {
  RNodeIdentity id;
#if defined(ESP32)
  id.platform = rnode::PLATFORM_ESP32;
  id.mcu = rnode::MCU_ESP32;
#elif defined(NRF52_PLATFORM)
  id.platform = rnode::PLATFORM_NRF52;
  id.mcu = rnode::MCU_NRF52;
#elif defined(RP2040_PLATFORM)
  id.platform = rnode::PLATFORM_RP2040;
  id.mcu = rnode::MCU_RP2040;
#else
  id.platform = rnode::PLATFORM_STM32;
  id.mcu = rnode::MCU_STM32;
#endif
  id.txp_max = LORA_TX_POWER;
  return id;
}

inline const char* platformName() {
#if defined(ESP32)
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
  return "ESP32-S3";
  #elif defined(CONFIG_IDF_TARGET_ESP32C3)
  return "ESP32-C3";
  #elif defined(CONFIG_IDF_TARGET_ESP32C6)
  return "ESP32-C6";
  #else
  return "ESP32";
  #endif
#elif defined(NRF52_PLATFORM)
  return "nRF52840";
#elif defined(RP2040_PLATFORM)
  return "RP2040";
#else
  return "STM32WL";
#endif
}

}  // namespace arb
