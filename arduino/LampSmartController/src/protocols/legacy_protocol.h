#pragma once

#include "lamp_protocol.h"

namespace lampsmart {

enum class LegacyV1Variant : uint8_t {
  V1A = 0, // 0x77 0xF8 header + secondary CRC2 (matches flicker581/aronsky v1a)
  V1B = 1, // 0xF9 0x08 0x49 header + 0xAA trailer (matches lampify/zt8989/StevenReitsma)
};

/**
 * Legacy LampSmart Pro V1 encoder (CCT-only; no AES-128 signature).
 *
 * Ported from aronsky/esphome-components and flicker581/esphome-lampsmart (MIT License).
 */
class LampLegacyProtocol final : public LampProtocol {
public:
  explicit LampLegacyProtocol(LegacyV1Variant variant = LegacyV1Variant::V1A);

  const char* id() const override;
  const char* displayName() const override;
  bool supportsRgb() const override { return false; }
  bool supportsScenes() const override { return false; }

  bool encode(
      const LampCommand& cmd,
      LampTargetConfig& config,
      EncodedPacket& out,
      uint16_t forcedRand = 0,
      int forcedTx = -1) const override;

private:
  LegacyV1Variant variant_;
};

} // namespace lampsmart

