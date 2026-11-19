#pragma once

#include "lamp_protocol.h"

namespace lampsmart {

enum class V3AdvertisingMode : uint8_t {
  UUID_LIST_SIGNED = 0,     // AD type 0x03 (13x 16-bit Service UUIDs, AES-128 signed) - matches xLamp Android on-air
  SERVICE_DATA_SIGNED = 1,  // AD type 0x16 (Service Data 0x08F0, AES-128 signed) - matches V3Frame raw array / remotes
  UUID_LIST_UNSIGNED = 2,   // AD type 0x03 (V2 unsigned, signature = 0x0000)
};

/**
 * LampSmart Pro V3 (and V2 unsigned) 31-byte frame encoder.
 *
 * Ported from qinjunhaocn/xLamp (PolyForm Noncommercial License 1.0.0:
 * https://polyformproject.org/licenses/noncommercial/1.0.0) and cross-verified
 * with aronsky/esphome-components and NicoIIT/ha-ble-adv (MIT License).
 */
class LampSmartV3Protocol final : public LampProtocol {
public:
  explicit LampSmartV3Protocol(V3AdvertisingMode mode = V3AdvertisingMode::UUID_LIST_SIGNED);

  const char* id() const override;
  const char* displayName() const override;
  bool supportsRgb() const override { return true; }
  bool supportsScenes() const override { return true; }

  bool encode(
      const LampCommand& cmd,
      LampTargetConfig& config,
      EncodedPacket& out,
      uint16_t forcedRand = 0,
      int forcedTx = -1) const override;

  /// Direct 31-byte V3 frame builder matching V3Frame.build() in xLamp.
  static void buildRawFrame(
      uint8_t outFrame[31],
      uint16_t type,
      uint32_t var1,
      uint8_t var2,
      uint16_t command,
      uint8_t p21,
      uint8_t p22,
      uint8_t p23,
      uint16_t randSeed,
      uint8_t txCount,
      bool signWithAes = true,
      uint8_t adFlag = 0x02,
      uint8_t adType = 0x03);

  /// Extract the 13 little-endian 16-bit Service UUIDs from frame[5..30].
  static void extractServiceUuid16(const uint8_t frame[31], uint16_t outUuids[13]);

  /// Build the 9-byte xLamp end-marker PDU: [0xbbaa, 0x3412].
  static size_t buildEndMarker(uint8_t outFrame[31], uint8_t adFlag = 0x02);

  static constexpr uint16_t kDefaultRand = 0x2B53;

private:
  V3AdvertisingMode mode_;
};

} // namespace lampsmart

