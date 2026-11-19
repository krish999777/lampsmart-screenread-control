/*
 * Legacy LampSmart Pro V1 encoder (v1a / v1b).
 * Derived from aronsky/esphome-components and flicker581/esphome-lampsmart (MIT License).
 */
#include "legacy_protocol.h"

#include <cstring>

namespace lampsmart {

namespace {

uint8_t reverseByte(uint8_t x) {
  x = static_cast<uint8_t>(((x & 0x55) << 1) | ((x & 0xAA) >> 1));
  x = static_cast<uint8_t>(((x & 0x33) << 2) | ((x & 0xCC) >> 2));
  x = static_cast<uint8_t>(((x & 0x0F) << 4) | ((x & 0xF0) >> 4));
  return x;
}

void bleWhiten(uint8_t* buf, size_t start, size_t len, uint8_t seed) {
  uint8_t r = seed;
  for (size_t i = 0; i < start + len; ++i) {
    uint8_t b = 0;
    for (size_t j = 0; j < 8; ++j) {
      r <<= 1;
      if (r & 0x80) {
        r ^= 0x11;
        b |= static_cast<uint8_t>(1u << j);
      }
      r &= 0x7F;
    }
    if (i >= start) {
      buf[i - start] ^= b;
    }
  }
}

} // namespace

LampLegacyProtocol::LampLegacyProtocol(LegacyV1Variant variant) : variant_(variant) {}

const char* LampLegacyProtocol::id() const {
  return (variant_ == LegacyV1Variant::V1A) ? "v1a" : "v1b";
}

const char* LampLegacyProtocol::displayName() const {
  return (variant_ == LegacyV1Variant::V1A)
             ? "LampSmart Legacy V1a (CCT Only)"
             : "LampSmart Legacy V1b (CCT Only)";
}

bool LampLegacyProtocol::encode(
    const LampCommand& cmd,
    LampTargetConfig& config,
    EncodedPacket& out,
    uint16_t forcedRand,
    int forcedTx) const {
  uint8_t opcode = 0;
  uint8_t par1 = 0;
  uint8_t par2 = 0;

  switch (cmd.type) {
    case LampCommandType::PAIR:
      opcode = 0x28;
      break;
    case LampCommandType::UNPAIR:
      opcode = 0x45;
      break;
    case LampCommandType::POWER:
      opcode = cmd.on ? 0x10 : 0x11;
      break;
    case LampCommandType::DIMMING: {
      opcode = 0x21;
      const DimmingChannels ch = computeDimmingChannels(cmd.brightness, cmd.temperature, config.reversedCct);
      par1 = ch.cool;
      par2 = ch.warm;
      break;
    }
    case LampCommandType::NIGHT_LIGHT:
      opcode = 0x23;
      break;
    default:
      // V1 legacy fixtures in this implementation are CCT-only.
      return false;
  }

  uint8_t buf[31]{};
  size_t base = 0;
  if (variant_ == LegacyV1Variant::V1A) {
    const uint8_t hdr[7] = {0x02, 0x01, config.adFlag, 0x1B, 0x03, 0x77, 0xF8};
    std::memcpy(buf, hdr, 7);
    base = 7;
  } else {
    const uint8_t hdr[8] = {0x02, 0x01, config.adFlag, 0x1B, 0x03, 0xF9, 0x08, 0x49};
    std::memcpy(buf, hdr, 8);
    base = 8;
  }

  const uint8_t tx = (forcedTx >= 0) ? static_cast<uint8_t>(forcedTx & 0xFF) : config.txCount++;
  const uint16_t seed = static_cast<uint16_t>(((forcedRand != 0) ? forcedRand : 0x2B53) % 65525u);
  const uint32_t v1 = config.var1();
  const uint16_t groupIdx = static_cast<uint16_t>((v1 & 0xF0FFu) | ((static_cast<uint16_t>(config.deviceAddress & 0x0F)) << 8));

  static constexpr uint8_t kV1Prefix[8] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};
  std::memcpy(buf + base, kV1Prefix, 8);

  uint8_t* body = buf + base + 8;
  body[0] = opcode;
  body[1] = static_cast<uint8_t>(groupIdx & 0xFF);
  body[2] = static_cast<uint8_t>((groupIdx >> 8) & 0xFF);
  if (opcode == 0x28) {
    body[3] = static_cast<uint8_t>(groupIdx & 0xFF);
    body[4] = static_cast<uint8_t>((groupIdx >> 8) & 0xFF);
    body[5] = 0x81;
  } else {
    body[3] = par1;
    body[4] = par2;
    body[5] = 0x00;
  }
  body[6] = tx;
  body[7] = 0x00;
  body[8] = static_cast<uint8_t>((seed & 0xFF) ^ 1u);
  body[9] = static_cast<uint8_t>((seed & 0xFF) ^ 1u);
  body[10] = static_cast<uint8_t>((seed >> 8) & 0xFF);
  body[11] = static_cast<uint8_t>(seed & 0xFF);

  const uint16_t crc = crc16Ccitt(body, 12, static_cast<uint16_t>(~seed));
  body[12] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  body[13] = static_cast<uint8_t>(crc & 0xFF);

  if (variant_ == LegacyV1Variant::V1A) {
    const uint16_t crcMac = crc16Ccitt(buf + base + 1, 5, 0xFFFF);
    const uint16_t crc2 = crc16Ccitt(body, 14, crcMac);
    buf[29] = static_cast<uint8_t>((crc2 >> 8) & 0xFF);
    buf[30] = static_cast<uint8_t>(crc2 & 0xFF);
  } else {
    buf[30] = 0xAA;
  }

  for (size_t i = base; i < 31; ++i) {
    buf[i] = reverseByte(buf[i]);
  }
  bleWhiten(buf + base, 8 + base, 31 - base, 83);

  std::memcpy(out.data, buf, 31);
  out.length = 31;
  out.uuidCount = 0;
  out.randSeed = seed;
  out.txCount = tx;
  out.opcode = opcode;
  return true;
}

} // namespace lampsmart

