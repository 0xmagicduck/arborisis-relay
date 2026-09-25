// Host stand-in for RadioLib's PhysicalLayer: the calls the arbiter makes.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define RADIOLIB_ERR_NONE           (0)
#define RADIOLIB_PREAMBLE_DETECTED  (-14)
#define RADIOLIB_CHANNEL_FREE       (-15)
#define RADIOLIB_LORA_DETECTED      (-702)

class PhysicalLayer {
public:
  virtual ~PhysicalLayer() {}
  virtual void setPacketReceivedAction(void (*func)(void)) = 0;
  virtual int16_t startReceive() = 0;
  virtual int16_t standby() = 0;
  virtual int16_t startChannelScan() = 0;
  virtual int16_t getChannelScanResult() = 0;
  virtual int16_t startTransmit(const uint8_t* data, size_t len, uint8_t addr = 0) = 0;
  virtual int16_t finishTransmit() = 0;
  virtual size_t getPacketLength(bool update = true) = 0;
  virtual int16_t readData(uint8_t* data, size_t len) = 0;
  virtual float getRSSI() = 0;
  virtual float getSNR() = 0;
  virtual int16_t setPreambleLength(size_t len) = 0;
};
