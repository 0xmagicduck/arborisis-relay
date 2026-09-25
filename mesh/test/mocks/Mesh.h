// Host stand-in for MeshCore's mesh::MainBoard, as far as the arbiter uses it.
#pragma once
#include <stdint.h>
namespace mesh {
class MainBoard {
public:
  virtual ~MainBoard() {}
  virtual void onBeforeTransmit() {}
  virtual void onAfterTransmit() {}
  virtual uint16_t getBattMilliVolts() { return 0; }
  virtual bool isExternalPowered() { return false; }
  virtual void reboot() {}
};
}
