#include "../src/protocols/aes128.h"
#include "../src/protocols/lamp_protocol.h"
#include "../src/protocols/lampsmart_v3.h"
#include "../src/protocols/legacy_protocol.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace lampsmart;

namespace {

std::vector<uint8_t> fromHex(const char* hex) {
  std::vector<uint8_t> out;
  size_t len = std::strlen(hex);
  assert(len % 2 == 0);
  for (size_t i = 0; i < len; i += 2) {
    unsigned int val = 0;
    std::sscanf(hex + i, "%2x", &val);
    out.push_back(static_cast<uint8_t>(val));
  }
  return out;
}

std::string toHex(const uint8_t* data, size_t len) {
  std::string out;
  char buf[3];
  for (size_t i = 0; i < len; ++i) {
    std::snprintf(buf, sizeof(buf), "%02x", data[i]);
    out += buf;
  }
  return out;
}

void testUnicornReferenceVectors() {
  struct Vec {
    const char* inputHex;
    uint16_t randSeed;
    const char* expectedHex;
  };

  // Reference vectors from xLamp/tool/verify.dart (verified against libencodeV3.so via Unicorn)
  // Note: V3Frame.build raw buffer uses adFlag=0x02, adType=0x16.
  static const Vec kVectors[] = {
    {
      "1030000001000210341278569a0011000000112200330100",
      0x23c4,
      "0201021b16f00830801cf4d3d037f57f8ac8d05ec3d655042d2d76c42374d7"
    },
    {
      "1030000002000010341278569a0045000000112200330100",
      0x2b53,
      "0201021b16f0083080b876e136d0bcb823ff29a49f67e7941e388b532b57d5"
    },
    {
      "1030000003000210341278569a0021000000112200330100",
      0x1234,
      "0201021b16f008308013854393fdb56a5491e2f80091c0e79d0a0534122c84"
    },
    {
      "1030000004000110341278569a0052000000112200330100",
      0xabcd,
      "0201021b16f0083080c853cbcf793d5ebce5ea7a305efad9e29901cdabb4c2"
    },
    {
      "1030000005000310341278569a0022000000112200330100",
      0x0000,
      "0201021b16f0083080a5e0f261ec236d525d01c3189614b8271080000047e8"
    },
  };

  for (size_t i = 0; i < sizeof(kVectors) / sizeof(kVectors[0]); ++i) {
    const auto in = fromHex(kVectors[i].inputHex);
    assert(in.size() == 24);
    const uint8_t tx = in[4];
    const uint16_t type = static_cast<uint16_t>(in[6] | (in[7] << 8));
    const uint32_t var1 = static_cast<uint32_t>(in[8]) |
                          (static_cast<uint32_t>(in[9]) << 8) |
                          (static_cast<uint32_t>(in[10]) << 16) |
                          (static_cast<uint32_t>(in[11]) << 24);
    const uint8_t var2 = in[12];
    const uint16_t command = static_cast<uint16_t>(in[14] | (in[15] << 8));
    const uint8_t p21 = in[17];
    const uint8_t p22 = in[18];
    const uint8_t p23 = in[19];

    uint8_t frame[31]{};
    LampSmartV3Protocol::buildRawFrame(
        frame, type, var1, var2, command, p21, p22, p23,
        kVectors[i].randSeed, tx, true, 0x02, 0x16);

    const std::string got = toHex(frame, 31);
    if (got != kVectors[i].expectedHex) {
      std::fprintf(stderr, "Vector %zu mismatch:\n  got: %s\n  exp: %s\n",
                   i, got.c_str(), kVectors[i].expectedHex);
      assert(false);
    }

    // Verify CRC16 is identical regardless of AD type 0x03 vs 0x16 (since CRC covers [7..28])
    uint8_t uuidFrame[31]{};
    LampSmartV3Protocol::buildRawFrame(
        uuidFrame, type, var1, var2, command, p21, p22, p23,
        kVectors[i].randSeed, tx, true, 0x02, 0x03);
    assert(uuidFrame[4] == 0x03);
    assert(std::memcmp(frame + 5, uuidFrame + 5, 26) == 0);
  }
  std::printf("[PASS] testUnicornReferenceVectors (5/5)\n");
}

void testDimmingChannels() {
  // Matches xLamp/test/dimming_test.dart
  {
    auto ch = computeDimmingChannels(100, 50);
    assert(ch.cool == 255 && ch.warm == 255);
  }
  {
    auto ch = computeDimmingChannels(100, 0);
    assert(ch.cool == 0 && ch.warm == 255);
  }
  {
    auto ch = computeDimmingChannels(100, 100);
    assert(ch.cool == 255 && ch.warm == 0);
  }
  {
    // Even at 0%, 10% floor keeps channels > 0 (25) so lamp doesn't turn off
    auto ch = computeDimmingChannels(0, 50);
    assert(ch.cool == 25 && ch.warm == 25);
    auto chMin1 = computeDimmingChannels(1, 50);
    assert(chMin1.cool > 25 && chMin1.warm > 25);
  }
  {
    auto ch = computeDimmingChannels(50, 25);
    assert(ch.cool == 70 && ch.warm == 140);
  }
  {
    auto chRev = computeDimmingChannels(100, 0, true);
    assert(chRev.cool == 255 && chRev.warm == 0);
  }
  std::printf("[PASS] testDimmingChannels\n");
}

void testColorMath() {
  assert(scaleRgbChannel(255, 0) == 25);
  assert(scaleRgbChannel(255, 100) == 255);

  uint8_t r = 0, g = 0, b = 0;
  hsvToRgb(0, 100, 100, r, g, b);
  assert(r == 255 && g == 0 && b == 0);

  hsvToRgb(120, 100, 100, r, g, b);
  assert(r == 0 && g == 255 && b == 0);

  hsvToRgb(240, 100, 100, r, g, b);
  assert(r == 0 && g == 0 && b == 255);

  hsvToRgb(180, 0, 100, r, g, b);
  assert(r == 255 && g == 255 && b == 255);

  std::printf("[PASS] testColorMath\n");
}

void testServiceUuidsAndEndMarker() {
  uint8_t frame[31]{};
  LampSmartV3Protocol::buildRawFrame(
      frame, 256, 0x123456, 1, 0x10, 0, 0, 0, 0x2B53, 0x10, true, 0x02, 0x03);
  uint16_t uuids[13]{};
  LampSmartV3Protocol::extractServiceUuid16(frame, uuids);
  assert(uuids[0] == 0x08F0);
  for (int i = 0; i < 13; ++i) {
    uint16_t expected = static_cast<uint16_t>(frame[5 + 2 * i] | (frame[6 + 2 * i] << 8));
    assert(uuids[i] == expected);
  }

  uint8_t marker[31]{};
  size_t mlen = LampSmartV3Protocol::buildEndMarker(marker, 0x02);
  assert(mlen == 9);
  const uint8_t expMarker[9] = {0x02, 0x01, 0x02, 0x05, 0x03, 0xAA, 0xBB, 0x12, 0x34};
  assert(std::memcmp(marker, expMarker, 9) == 0);
  std::printf("[PASS] testServiceUuidsAndEndMarker\n");
}

void testAllV3CommandsAndWraparound() {
  const LampProtocol* v3 = getProtocolById("v3");
  assert(v3 != nullptr);
  assert(v3->supportsRgb());
  assert(v3->supportsScenes());

  LampTargetConfig cfg;
  cfg.deviceType = 256;
  cfg.deviceId = 0x112233;
  cfg.channel = 2;
  cfg.deviceAddress = 5;
  cfg.txCount = 255;

  EncodedPacket pkt;

  // Test txCount wrap-around 255 -> 0
  LampCommand cmd;
  cmd.type = LampCommandType::PAIR;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.length == 31 && pkt.opcode == 0x28 && pkt.txCount == 255);
  assert(cfg.txCount == 0);

  cmd.type = LampCommandType::UNPAIR;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x45 && pkt.txCount == 0);

  cmd.type = LampCommandType::POWER;
  cmd.on = true;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x10);

  cmd.on = false;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x11);

  cmd.type = LampCommandType::DIMMING;
  cmd.brightness = 50;
  cmd.temperature = 25;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x21);

  cmd.type = LampCommandType::RGB;
  cmd.r = 255; cmd.g = 100; cmd.b = 50; cmd.brightness = 80;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x22);

  cmd.type = LampCommandType::NIGHT_LIGHT;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x23);

  cmd.type = LampCommandType::SCENE_ON;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x10);

  cmd.type = LampCommandType::SCENE_SWITCH;
  cmd.sceneIndex = 2;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x47);

  cmd.type = LampCommandType::SCENE_SET;
  cmd.sceneIndex = 1;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x46);

  cmd.type = LampCommandType::SCENE_RESET;
  assert(v3->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x48);

  // Verify V3 Service Data variant produces exact same payload except AD Type byte 4 = 0x16
  const LampProtocol* v3Srv = getProtocolById("v3_srvdata");
  LampTargetConfig cfgA = cfg;
  LampTargetConfig cfgB = cfg;
  EncodedPacket pktA, pktB;
  cmd.type = LampCommandType::POWER;
  cmd.on = true;
  assert(v3->encode(cmd, cfgA, pktA, 0x2B53, 0x42));
  assert(v3Srv->encode(cmd, cfgB, pktB, 0x2B53, 0x42));
  assert(pktA.data[4] == 0x03 && pktB.data[4] == 0x16);
  assert(std::memcmp(pktA.data + 5, pktB.data + 5, 26) == 0);

  std::printf("[PASS] testAllV3CommandsAndWraparound\n");
}

void testLegacyV1Protocol() {
  const LampProtocol* v1a = getProtocolById("v1a");
  const LampProtocol* v1b = getProtocolById("v1b");
  assert(v1a && v1b);
  assert(!v1a->supportsRgb());
  assert(!v1b->supportsRgb());

  LampTargetConfig cfg;
  EncodedPacket pkt;
  LampCommand cmd;

  cmd.type = LampCommandType::PAIR;
  assert(v1a->encode(cmd, cfg, pkt, 0x1234, 1));
  assert(pkt.length == 31 && pkt.opcode == 0x28);
  assert(v1b->encode(cmd, cfg, pkt, 0x1234, 2));
  assert(pkt.length == 31 && pkt.opcode == 0x28);

  cmd.type = LampCommandType::DIMMING;
  cmd.brightness = 75;
  cmd.temperature = 60;
  assert(v1a->encode(cmd, cfg, pkt));
  assert(pkt.opcode == 0x21);

  cmd.type = LampCommandType::RGB;
  assert(!v1a->encode(cmd, cfg, pkt));
  assert(!v1b->encode(cmd, cfg, pkt));

  std::printf("[PASS] testLegacyV1Protocol\n");
}

} // namespace

int main() {
  testUnicornReferenceVectors();
  testDimmingChannels();
  testColorMath();
  testServiceUuidsAndEndMarker();
  testAllV3CommandsAndWraparound();
  testLegacyV1Protocol();
  std::printf("ALL PROTOCOL TESTS PASSED!\n");
  return 0;
}

