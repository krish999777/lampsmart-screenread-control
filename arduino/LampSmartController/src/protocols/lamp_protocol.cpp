#include "lamp_protocol.h"
#include "lampsmart_v3.h"
#include "legacy_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lampsmart {

namespace {
template <typename T>
inline T clampVal(T v, T lo, T hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}
} // namespace

uint16_t crc16Ccitt(const uint8_t* data, size_t len, uint16_t init) {
  uint16_t crc = init;
  for (size_t idx = 0; idx < len; ++idx) {
    crc ^= static_cast<uint16_t>(data[idx]) << 8;
    for (int i = 0; i < 8; ++i) {
      if (crc & 0x8000u) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

DimmingChannels computeDimmingChannels(uint8_t brightnessPct, uint8_t temperaturePct, bool reversed) {
  const double b = clampVal(static_cast<double>(brightnessPct) / 100.0, 0.0, 1.0);
  const double t = clampVal(static_cast<double>(temperaturePct), 0.0, 100.0);
  const double factor = 0.1 + 0.9 * b;
  const int maxChannel = static_cast<int>(std::floor(255.0 * factor));

  int cool = 0;
  int warm = 0;
  if (t > 50.0) {
    cool = maxChannel;
    warm = static_cast<int>(std::floor(255.0 * factor * (1.0 - (t - 50.0) / 50.0)));
  } else {
    cool = static_cast<int>(std::floor(255.0 * factor * (t / 50.0)));
    warm = maxChannel;
  }

  const uint8_t c = static_cast<uint8_t>(clampVal(cool, 0, 255));
  const uint8_t w = static_cast<uint8_t>(clampVal(warm, 0, 255));
  return reversed ? DimmingChannels{w, c} : DimmingChannels{c, w};
}

uint8_t scaleRgbChannel(uint8_t channel, uint8_t brightnessPct) {
  const double b = clampVal(static_cast<double>(brightnessPct) / 100.0, 0.0, 1.0);
  const double factor = 0.1 + 0.9 * b;
  const int scaled = static_cast<int>(std::floor(static_cast<double>(channel) * factor));
  return static_cast<uint8_t>(clampVal(scaled, 0, 255));
}

void hsvToRgb(float hDeg, float sPct, float vPct, uint8_t& outR, uint8_t& outG, uint8_t& outB) {
  float h = std::fmod(hDeg, 360.0f);
  if (h < 0.0f) h += 360.0f;
  const float s = clampVal(sPct / 100.0f, 0.0f, 1.0f);
  const float v = clampVal(vPct / 100.0f, 0.0f, 1.0f);

  const float c = v * s;
  const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  const float m = v - c;

  float rf = 0, gf = 0, bf = 0;
  if (h < 60.0f) {
    rf = c; gf = x; bf = 0;
  } else if (h < 120.0f) {
    rf = x; gf = c; bf = 0;
  } else if (h < 180.0f) {
    rf = 0; gf = c; bf = x;
  } else if (h < 240.0f) {
    rf = 0; gf = x; bf = c;
  } else if (h < 300.0f) {
    rf = x; gf = 0; bf = c;
  } else {
    rf = c; gf = 0; bf = x;
  }

  outR = static_cast<uint8_t>(clampVal(static_cast<int>(std::lround((rf + m) * 255.0f)), 0, 255));
  outG = static_cast<uint8_t>(clampVal(static_cast<int>(std::lround((gf + m) * 255.0f)), 0, 255));
  outB = static_cast<uint8_t>(clampVal(static_cast<int>(std::lround((bf + m) * 255.0f)), 0, 255));
}

const LampProtocol* getProtocolById(const char* id) {
  static const LampSmartV3Protocol kV3(V3AdvertisingMode::UUID_LIST_SIGNED);
  static const LampSmartV3Protocol kV3Srv(V3AdvertisingMode::SERVICE_DATA_SIGNED);
  static const LampSmartV3Protocol kV2(V3AdvertisingMode::UUID_LIST_UNSIGNED);
  static const LampLegacyProtocol kV1a(LegacyV1Variant::V1A);
  static const LampLegacyProtocol kV1b(LegacyV1Variant::V1B);

  if (id == nullptr) return &kV3;
  if (std::strcmp(id, "v3") == 0) return &kV3;
  if (std::strcmp(id, "v3_srvdata") == 0) return &kV3Srv;
  if (std::strcmp(id, "v2") == 0) return &kV2;
  if (std::strcmp(id, "v1a") == 0) return &kV1a;
  if (std::strcmp(id, "v1b") == 0) return &kV1b;
  return &kV3;
}

bool isValidProtocolId(const char* id) {
  if (id == nullptr) return false;
  return std::strcmp(id, "v3") == 0 ||
         std::strcmp(id, "v3_srvdata") == 0 ||
         std::strcmp(id, "v2") == 0 ||
         std::strcmp(id, "v1a") == 0 ||
         std::strcmp(id, "v1b") == 0;
}

} // namespace lampsmart
