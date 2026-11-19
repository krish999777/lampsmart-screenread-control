#pragma once

#include <cstddef>
#include <cstdint>

namespace lampsmart {

enum class LampDeviceType : uint16_t {
  LAMP = 256,  // 0x0100
  FAN  = 512,  // 0x0200
  FLP  = 1024, // 0x0400 (exhaust)
  BATH = 2048, // 0x0800
};

enum class LampCommandType : uint8_t {
  PAIR = 0,
  UNPAIR,
  POWER,
  DIMMING,
  RGB,
  RGB_POWER,
  RGB_AUTO,
  SCENE_ON,
  SCENE_SET,
  SCENE_SWITCH,
  SCENE_RESET,
  NIGHT_LIGHT,
  FAN_SPEED,
  FAN_MODE,
  RAW,
};

struct LampCommand {
  LampCommandType type = LampCommandType::POWER;
  bool on = true;
  uint8_t brightness = 80;    // 1..100
  uint8_t temperature = 50;   // 0 (warm)..100 (cool)
  uint8_t r = 255;
  uint8_t g = 214;
  uint8_t b = 170;
  uint8_t sceneIndex = 0;     // 0..3 for Scenes 1..4
  uint8_t fanSpeed = 0;
  uint8_t fanMode = 0;
  // Raw diagnostic fields
  uint16_t rawCommand = 0;
  uint8_t rawP21 = 0;
  uint8_t rawP22 = 0;
  uint8_t rawP23 = 0;
};

struct LampTargetConfig {
  uint16_t deviceType = static_cast<uint16_t>(LampDeviceType::LAMP);
  uint32_t deviceId = 0x123456;   // Base ID; var1 = (deviceId + channel) & 0x7FFFFFFF
  uint8_t channel = 0;            // 0..15
  uint8_t deviceAddress = 1;      // var2 / group_index (0..255)
  uint8_t txCount = 0x10;         // Rolling 0..255 counter
  bool reversedCct = false;       // Swap cool/warm channels for reversed fixtures
  uint8_t adFlag = 0x02;          // BLE AD Flags byte (0x02 default; 0x19 alternative)

  uint32_t var1() const {
    return (deviceId + static_cast<uint32_t>(channel)) & 0x7FFFFFFFu;
  }
};

struct EncodedPacket {
  uint8_t data[31]{};
  size_t length = 0;
  uint16_t serviceUuids[13]{};
  size_t uuidCount = 0;
  uint16_t randSeed = 0;
  uint8_t txCount = 0;
  uint16_t opcode = 0;
};

class LampProtocol {
public:
  virtual ~LampProtocol() = default;

  virtual const char* id() const = 0;
  virtual const char* displayName() const = 0;
  virtual bool supportsRgb() const = 0;
  virtual bool supportsScenes() const = 0;

  virtual bool encode(
      const LampCommand& cmd,
      LampTargetConfig& config,
      EncodedPacket& out,
      uint16_t forcedRand = 0,
      int forcedTx = -1) const = 0;
};

/// Shared math helpers matching LampSmart Pro / xLamp behavior.
struct DimmingChannels {
  uint8_t cool;
  uint8_t warm;
};

DimmingChannels computeDimmingChannels(uint8_t brightnessPct, uint8_t temperaturePct, bool reversed = false);
uint8_t scaleRgbChannel(uint8_t channel, uint8_t brightnessPct);
void hsvToRgb(float hDeg, float sPct, float vPct, uint8_t& outR, uint8_t& outG, uint8_t& outB);

/// CRC-16/CCITT (poly 0x1021) matching V3Frame.crc16 and FanLampEncoder::crc16.
uint16_t crc16Ccitt(const uint8_t* data, size_t len, uint16_t init);

/// Factory returning a static protocol instance by ID:
/// "v3" (default), "v3_srvdata", "v2", "v1a", "v1b".
const LampProtocol* getProtocolById(const char* id);
bool isValidProtocolId(const char* id);

} // namespace lampsmart

