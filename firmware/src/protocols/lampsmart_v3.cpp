/*
 * LampSmart Pro V3 frame encoder.
 *
 * Derived from qinjunhaocn/xLamp (lib/protocol/v3_frame.dart, lib/protocol/lamp_commands.dart),
 * licensed under the PolyForm Noncommercial License 1.0.0:
 * https://polyformproject.org/licenses/noncommercial/1.0.0
 *
 * Cross-verified against aronsky/esphome-components and NicoIIT/ha-ble-adv (MIT License).
 */
#include "lampsmart_v3.h"
#include "aes128.h"

#include <cstring>

namespace lampsmart {

namespace {

static constexpr uint8_t kXboxes[128] = {
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1,
  0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
  0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0xd0, 0xef, 0xaa, 0xfb,
  0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21,
  0x10, 0xff, 0xf3, 0xd2, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
  0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
  0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9,
  0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
  0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static constexpr uint8_t kKeyTail[13] = {
  0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99,
  0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

namespace Op {
  static constexpr uint16_t kPair         = 0x28;
  static constexpr uint16_t kUnpair       = 0x45;
  static constexpr uint16_t kOn           = 0x10;
  static constexpr uint16_t kOff          = 0x11;
  static constexpr uint16_t kDimming      = 0x21;
  static constexpr uint16_t kDimmingNight = 0x23;
  static constexpr uint16_t kRgbSwitch    = 0x22;
  static constexpr uint16_t kRgbOnOff     = 0x1C;
  static constexpr uint16_t kRgbAutoOn    = 0x1E;
  static constexpr uint16_t kRgbAutoOff   = 0x1F;
  static constexpr uint16_t kSceneOn      = 0x10;
  static constexpr uint16_t kSceneSet     = 0x46;
  static constexpr uint16_t kSceneSwitch  = 0x47;
  static constexpr uint16_t kSceneReset   = 0x48;
  static constexpr uint16_t kFanSpeed     = 0x31;
  static constexpr uint16_t kFanMode      = 0x33;
}

} // namespace

LampSmartV3Protocol::LampSmartV3Protocol(V3AdvertisingMode mode) : mode_(mode) {}

const char* LampSmartV3Protocol::id() const {
  switch (mode_) {
    case V3AdvertisingMode::UUID_LIST_SIGNED:    return "v3";
    case V3AdvertisingMode::SERVICE_DATA_SIGNED: return "v3_srvdata";
    case V3AdvertisingMode::UUID_LIST_UNSIGNED:  return "v2";
  }
  return "v3";
}

const char* LampSmartV3Protocol::displayName() const {
  switch (mode_) {
    case V3AdvertisingMode::UUID_LIST_SIGNED:    return "LampSmart Pro V3 (UUID List, Signed)";
    case V3AdvertisingMode::SERVICE_DATA_SIGNED: return "LampSmart Pro V3 (Service Data 0x16)";
    case V3AdvertisingMode::UUID_LIST_UNSIGNED:  return "LampSmart Pro V2 (Unsigned)";
  }
  return "LampSmart Pro V3";
}

void LampSmartV3Protocol::buildRawFrame(
    uint8_t p[31],
    uint16_t type,
    uint32_t var1,
    uint8_t var2,
    uint16_t command,
    uint8_t p21,
    uint8_t p22,
    uint8_t p23,
    uint16_t randSeed,
    uint8_t txCount,
    bool signWithAes,
    uint8_t adFlag,
    uint8_t adType) {
  std::memset(p, 0, 31);
  p[0] = 0x02;
  p[1] = 0x01;
  p[2] = adFlag;
  p[3] = 0x1B;
  p[4] = adType;
  p[5] = 0xF0;
  p[6] = 0x08;
  p[7] = 0x30;
  p[8] = 0x80;
  p[9] = 0x00;
  p[10] = txCount;
  p[11] = static_cast<uint8_t>(type & 0xFF);
  p[12] = static_cast<uint8_t>((type >> 8) & 0xFF);
  p[13] = static_cast<uint8_t>(var1 & 0xFF);
  p[14] = static_cast<uint8_t>((var1 >> 8) & 0xFF);
  p[15] = static_cast<uint8_t>((var1 >> 16) & 0xFF);
  p[16] = static_cast<uint8_t>((var1 >> 24) & 0xFF);
  p[17] = var2;
  p[18] = static_cast<uint8_t>(command & 0xFF);
  p[19] = static_cast<uint8_t>((command >> 8) & 0xFF);
  p[20] = 0x00;
  p[21] = p21;
  p[22] = p22;
  p[23] = p23;
  p[24] = 0x00;
  p[25] = 0x00;
  p[26] = 0x00;

  if (signWithAes) {
    uint8_t key[16];
    key[0] = static_cast<uint8_t>(randSeed & 0xFF);
    key[1] = static_cast<uint8_t>((randSeed >> 8) & 0xFF);
    key[2] = txCount;
    std::memcpy(key + 3, kKeyTail, 13);

    uint8_t aesOut[16]{};
    aes128EcbEncrypt(key, p + 8, aesOut);
    uint16_t check = static_cast<uint16_t>(aesOut[0] | (aesOut[1] << 8));
    if (check == 0) {
      check = 0xFFFF;
    }
    p[24] = static_cast<uint8_t>(check & 0xFF);
    p[25] = static_cast<uint8_t>((check >> 8) & 0xFF);
  }

  const uint8_t r = static_cast<uint8_t>(randSeed & 0xFF);
  for (int i = 9; i <= 26; ++i) {
    p[i] ^= static_cast<uint8_t>(r ^ kXboxes[(i + r) & 0x1F]);
  }

  p[27] = static_cast<uint8_t>(randSeed & 0xFF);
  p[28] = static_cast<uint8_t>((randSeed >> 8) & 0xFF);

  const uint16_t crc = crc16Ccitt(p + 7, 22, static_cast<uint16_t>(~randSeed));
  p[29] = static_cast<uint8_t>(crc & 0xFF);
  p[30] = static_cast<uint8_t>((crc >> 8) & 0xFF);
}

void LampSmartV3Protocol::extractServiceUuid16(const uint8_t frame[31], uint16_t outUuids[13]) {
  for (int i = 0; i < 13; ++i) {
    outUuids[i] = static_cast<uint16_t>(frame[5 + 2 * i] | (frame[6 + 2 * i] << 8));
  }
}

size_t LampSmartV3Protocol::buildEndMarker(uint8_t outFrame[31], uint8_t adFlag) {
  std::memset(outFrame, 0, 31);
  // Flags: 02 01 <adFlag>
  outFrame[0] = 0x02;
  outFrame[1] = 0x01;
  outFrame[2] = adFlag;
  // 2x 16-bit Service UUIDs: 0xBBAA, 0x3412 (little endian)
  outFrame[3] = 0x05;
  outFrame[4] = 0x03;
  outFrame[5] = 0xAA;
  outFrame[6] = 0xBB;
  outFrame[7] = 0x12;
  outFrame[8] = 0x34;
  return 9;
}

bool LampSmartV3Protocol::encode(
    const LampCommand& cmd,
    LampTargetConfig& config,
    EncodedPacket& out,
    uint16_t forcedRand,
    int forcedTx) const {
  uint16_t opcode = 0;
  uint8_t p21 = 0;
  uint8_t p22 = 0;
  uint8_t p23 = 0;

  switch (cmd.type) {
    case LampCommandType::PAIR:
      opcode = Op::kPair;
      break;
    case LampCommandType::UNPAIR:
      opcode = Op::kUnpair;
      break;
    case LampCommandType::POWER:
      opcode = cmd.on ? Op::kOn : Op::kOff;
      break;
    case LampCommandType::DIMMING: {
      opcode = Op::kDimming;
      const DimmingChannels ch = computeDimmingChannels(cmd.brightness, cmd.temperature, config.reversedCct);
      p22 = ch.cool;
      p23 = ch.warm;
      break;
    }
    case LampCommandType::RGB:
      opcode = Op::kRgbSwitch;
      p21 = scaleRgbChannel(cmd.r, cmd.brightness);
      p22 = scaleRgbChannel(cmd.g, cmd.brightness);
      p23 = scaleRgbChannel(cmd.b, cmd.brightness);
      break;
    case LampCommandType::RGB_POWER:
      opcode = Op::kRgbOnOff;
      p21 = cmd.on ? 1 : 0;
      break;
    case LampCommandType::RGB_AUTO:
      opcode = cmd.on ? Op::kRgbAutoOn : Op::kRgbAutoOff;
      break;
    case LampCommandType::SCENE_ON:
      opcode = Op::kSceneOn;
      p21 = 1;
      break;
    case LampCommandType::SCENE_SET:
      opcode = Op::kSceneSet;
      p21 = cmd.sceneIndex;
      break;
    case LampCommandType::SCENE_SWITCH:
      opcode = Op::kSceneSwitch;
      p21 = cmd.sceneIndex;
      break;
    case LampCommandType::SCENE_RESET:
      opcode = Op::kSceneReset;
      break;
    case LampCommandType::NIGHT_LIGHT:
      opcode = Op::kDimmingNight;
      break;
    case LampCommandType::FAN_SPEED:
      opcode = Op::kFanSpeed;
      p21 = cmd.fanSpeed;
      break;
    case LampCommandType::FAN_MODE:
      opcode = Op::kFanMode;
      p21 = cmd.fanMode;
      break;
    case LampCommandType::RAW:
      opcode = cmd.rawCommand;
      p21 = cmd.rawP21;
      p22 = cmd.rawP22;
      p23 = cmd.rawP23;
      break;
    default:
      return false;
  }

  const uint8_t tx = (forcedTx >= 0) ? static_cast<uint8_t>(forcedTx & 0xFF) : config.txCount++;
  const uint16_t seed = (forcedRand != 0) ? forcedRand : kDefaultRand;
  const bool sign = (mode_ != V3AdvertisingMode::UUID_LIST_UNSIGNED);
  const uint8_t adType = (mode_ == V3AdvertisingMode::SERVICE_DATA_SIGNED) ? 0x16 : 0x03;

  buildRawFrame(
      out.data,
      config.deviceType,
      config.var1(),
      config.deviceAddress,
      opcode,
      p21,
      p22,
      p23,
      seed,
      tx,
      sign,
      config.adFlag,
      adType);

  out.length = 31;
  out.uuidCount = 13;
  extractServiceUuid16(out.data, out.serviceUuids);
  out.randSeed = seed;
  out.txCount = tx;
  out.opcode = opcode;
  return true;
}

} // namespace lampsmart

