#pragma once

#include <Arduino.h>
#include <cstdint>
#include "protocols/lamp_protocol.h"

namespace lampsmart {

struct DeviceSettings {
  String lightName = "LampSmart Light";
  String protocol = "v3";
  uint16_t deviceType = static_cast<uint16_t>(LampDeviceType::LAMP);
  uint32_t deviceId = 0;        // 0 = auto-generated from MAC/random on first boot
  uint8_t channel = 0;          // 0..15
  uint8_t deviceAddress = 1;    // 0..255 (var2)
  bool paired = false;
  bool reversedCct = false;
  uint8_t adFlag = 0x02;
  uint8_t txCount = 0x10;
  uint8_t advRepeats = 8;       // xLamp default: 8 windows
  uint16_t advWindowMs = 500;   // xLamp default: 500ms per window
  bool advEndMarker = true;     // xLamp default: emit [0xbbaa, 0x3412] marker
  String mdnsHost = "lampsmart";
  String wifiSsid = "";
  String wifiPass = "";
};

struct LampState {
  bool power = false;
  uint8_t brightness = 80;    // 1..100
  uint8_t temperature = 50;   // 0 (warm)..100 (cool)
  uint16_t hue = 35;          // 0..360
  uint8_t sat = 35;           // 0..100
  uint8_t val = 100;          // 0..100
  uint8_t r = 255;
  uint8_t g = 214;
  uint8_t b = 170;
  String colorMode = "cct";   // "cct", "rgb", "scene", "night"
  uint8_t scene = 1;          // 1..4
  uint64_t lastCommandMs = 0;
};

struct TransmissionStatus {
  bool success = false;
  String status = "idle";     // "idle", "transmitting", "transmitted", "failed"
  String message = "No command sent yet";
  String command = "none";
  String protocol = "v3";
  uint8_t txCount = 0;
  uint64_t timestampMs = 0;
  String frameHex = "";
};

class ConfigStore {
public:
  bool load(DeviceSettings& settings, LampState& state);
  bool saveSettings(const DeviceSettings& settings);
  bool saveTxCount(uint8_t txCount);
  bool saveState(const LampState& state);
  bool saveWifi(const String& ssid, const String& pass);
  bool clearWifi();
};

} // namespace lampsmart

