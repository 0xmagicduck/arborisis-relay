// Host stand-in for MeshCore's RadioLibWrapper: the members the arbiter
// reaches (public ones, and the protected ones through WrapperAccess).
#pragma once
#include "../../Mesh.h"   // the stand-in next door, not MeshCore's src/Mesh.h
#include <RadioLib.h>

class RadioLibWrapper {
protected:
  PhysicalLayer* _radio;
  mesh::MainBoard* _board;
  virtual bool isReceivingPacket() = 0;
  virtual void doResetAGC() {}
public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board) : _radio(&radio), _board(&board) {}
  virtual ~RadioLibWrapper() {}
  virtual void setParams(float freq, float bw, uint8_t sf, uint8_t cr) = 0;
  virtual void setTxPower(int8_t dbm) = 0;
  virtual float getCurrentRSSI() = 0;
  virtual void powerOff() {}
  uint32_t getRngSeed() { return 1234; }
  virtual bool setRxBoostedGainMode(bool) { return false; }
  virtual bool getRxBoostedGainMode() const { return false; }
  virtual bool configSideDetectors(const uint8_t[], uint8_t, float) { return false; }
};
