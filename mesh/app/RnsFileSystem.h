// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// RnsFileSystem.h — microReticulum's storage on the board's file system
// (the one MeshCore uses: LittleFS on ESP32 and RP2040, InternalFS on
// nRF52). Reticulum keeps its transport identity, known destinations and,
// where there is room, its path table there.

#pragma once

#include "ArbPlatform.h"

#if ARB_WITH_RNS

#include <FileSystem.h>
#include <FileStream.h>
#include <Bytes.h>

namespace arb {

class RnsFileSystem : public RNS::FileSystemImpl {
public:
  bool init() override;
  bool file_exists(const char* path) override;
  size_t read_file(const char* path, RNS::Bytes& data) override;
  size_t write_file(const char* path, const RNS::Bytes& data) override;
  RNS::FileStream open_file(const char* path, RNS::FileStream::MODE mode) override;
  bool remove_file(const char* path) override;
  bool rename_file(const char* from, const char* to) override;
  bool directory_exists(const char* path) override;
  bool create_directory(const char* path) override;
  bool remove_directory(const char* path) override;
  std::list<std::string> list_directory(const char* path) override;
  size_t storage_size() override;
  size_t storage_available() override;
};

}  // namespace arb

#endif
