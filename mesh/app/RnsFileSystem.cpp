// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../LICENSE)
//
// Derived from RTNode's FileSystem.cpp (../../FileSystem.cpp), reduced to the
// two file APIs MeshCore's platforms use: Arduino fs::FS (ESP32, RP2040) and
// Adafruit_LittleFS (nRF52, STM32).

#include "RnsFileSystem.h"

#if ARB_WITH_RNS

#include <memory>

namespace arb {

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #define ARB_LFS_ADAFRUIT 1
  typedef Adafruit_LittleFS_Namespace::File ArbFile;
#else
  #define ARB_LFS_ADAFRUIT 0
  typedef fs::File ArbFile;
#endif

namespace {

class ArbFileStream : public RNS::FileStreamImpl {
public:
  explicit ArbFileStream(ArbFile* f) : _file(f) {}
  ~ArbFileStream() override { if (!_closed) close(); }
  const char* name() override { return _file->name(); }
  size_t size() override { return _file->size(); }
  void close() override { _closed = true; _file->close(); }
  size_t write(uint8_t b) override { return _file->write(b); }
  size_t write(const uint8_t* buf, size_t n) override { return _file->write(buf, n); }
  int available() override { return _file->available(); }
  int read() override { return _file->read(); }
  int peek() override { return _file->peek(); }
  void flush() override { _file->flush(); }
private:
  std::unique_ptr<ArbFile> _file;
  bool _closed = false;
};

bool openFile(ArbFile& f, const char* path, bool write, bool append) {
#if ARB_LFS_ADAFRUIT
  (void)append;
  return f.open(path, write ? FILE_O_WRITE : FILE_O_READ);
#else
  f = ARB_FS.open(path, write ? (append ? "a" : "w") : "r");
  return (bool)f;
#endif
}

void ensureParent(const char* path) {
  const char* slash = strrchr(path, '/');
  if (!slash || slash == path) return;
  size_t n = slash - path;
  if (n >= 96) return;
  char dir[96];
  memcpy(dir, path, n);
  dir[n] = 0;
  if (!ARB_FS.exists(dir)) ARB_FS.mkdir(dir);
}

}  // namespace

bool RnsFileSystem::init() {
  // Reticulum's storage path on a microcontroller is the root; its cache
  // directory must exist before Transport starts.
  if (!ARB_FS.exists("/cache")) ARB_FS.mkdir("/cache");
  return true;
}

bool RnsFileSystem::file_exists(const char* path) {
  return ARB_FS.exists(path);
}

size_t RnsFileSystem::read_file(const char* path, RNS::Bytes& data) {
  if (!ARB_FS.exists(path)) return 0;
#if ARB_LFS_ADAFRUIT
  ArbFile f(ARB_FS);
#else
  ArbFile f;
#endif
  if (!openFile(f, path, false, false)) return 0;
  size_t size = f.size();
  size_t got = f.read(data.writable(size), size);
  if (got != size) data.resize(got);
  f.close();
  return got;
}

size_t RnsFileSystem::write_file(const char* path, const RNS::Bytes& data) {
  ensureParent(path);
  if (ARB_FS.exists(path)) ARB_FS.remove(path);   // no truncate on every backend
#if ARB_LFS_ADAFRUIT
  ArbFile f(ARB_FS);
#else
  ArbFile f;
#endif
  if (!openFile(f, path, true, false)) return 0;
  size_t wrote = f.write(data.data(), data.size());
  f.close();
  return wrote;
}

RNS::FileStream RnsFileSystem::open_file(const char* path, RNS::FileStream::MODE mode) {
  const bool write = mode != RNS::FileStream::MODE_READ;
  const bool append = mode == RNS::FileStream::MODE_APPEND;
  if (!write && !ARB_FS.exists(path)) return {RNS::Type::NONE};
  if (write) ensureParent(path);
  if (mode == RNS::FileStream::MODE_WRITE && ARB_FS.exists(path)) ARB_FS.remove(path);
#if ARB_LFS_ADAFRUIT
  ArbFile* f = new ArbFile(ARB_FS);
#else
  ArbFile* f = new ArbFile();
#endif
  if (!openFile(*f, path, write, append)) {
    delete f;
    return {RNS::Type::NONE};
  }
  return RNS::FileStream(new ArbFileStream(f));
}

bool RnsFileSystem::remove_file(const char* path) { return ARB_FS.remove(path); }

bool RnsFileSystem::rename_file(const char* from, const char* to) { return ARB_FS.rename(from, to); }

bool RnsFileSystem::directory_exists(const char* path) {
  if (!ARB_FS.exists(path)) return false;
#if ARB_LFS_ADAFRUIT
  ArbFile f(ARB_FS);
  if (!f.open(path, FILE_O_READ)) return false;
#else
  ArbFile f = ARB_FS.open(path, "r");
  if (!f) return false;
#endif
  bool dir = f.isDirectory();
  f.close();
  return dir;
}

bool RnsFileSystem::create_directory(const char* path) { return ARB_FS.mkdir(path); }

bool RnsFileSystem::remove_directory(const char* path) {
#if ARB_LFS_ADAFRUIT
  return ARB_FS.rmdir_r(path);
#else
  // fs::FS removes only empty directories: empty it first.
  std::list<std::string> files = list_directory(path);
  for (auto& name : files) {
    std::string full = std::string(path) + "/" + name;
    ARB_FS.remove(full.c_str());
  }
  return ARB_FS.rmdir(path);
#endif
}

std::list<std::string> RnsFileSystem::list_directory(const char* path) {
  std::list<std::string> out;
#if ARB_LFS_ADAFRUIT
  ArbFile root(ARB_FS);
  if (!root.open(path, FILE_O_READ)) return out;
#else
  ArbFile root = ARB_FS.open(path, "r");
  if (!root) return out;
#endif
  ArbFile f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      const char* n = f.name();
      const char* base = strrchr(n, '/');
      out.push_back(base ? base + 1 : n);
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();
  return out;
}

#if ARB_LFS_ADAFRUIT
namespace {
int countBlock(void* p, lfs_block_t) { (*(lfs_size_t*)p)++; return 0; }
}
#endif

size_t RnsFileSystem::storage_size() {
#if defined(ESP32)
  return ARB_FS.totalBytes();
#elif defined(RP2040_PLATFORM)
  FSInfo i;
  return ARB_FS.info(i) ? i.totalBytes : 0;
#else
  const lfs_config* c = ARB_FS._getFS()->cfg;
  return (size_t)c->block_size * c->block_count;
#endif
}

size_t RnsFileSystem::storage_available() {
#if defined(ESP32)
  return ARB_FS.totalBytes() - ARB_FS.usedBytes();
#elif defined(RP2040_PLATFORM)
  FSInfo i;
  return ARB_FS.info(i) ? i.totalBytes - i.usedBytes : 0;
#else
  lfs_size_t used = 0;
  lfs_traverse(ARB_FS._getFS(), countBlock, &used);
  const lfs_config* c = ARB_FS._getFS()->cfg;
  return (size_t)c->block_size * (c->block_count - used);
#endif
}

}  // namespace arb

#endif  // ARB_WITH_RNS
