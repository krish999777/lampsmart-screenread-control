#include "device_config.h"

#include <Preferences.h>
#include <esp_mac.h>
#include <esp_system.h>

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

#ifndef WIFI_DEFAULT_SSID
#define WIFI_DEFAULT_SSID ""
#endif
#ifndef WIFI_DEFAULT_PASS
#define WIFI_DEFAULT_PASS ""
#endif

namespace lampsmart {

namespace {
constexpr const char* kNsConfig = "lampsmart_cfg";
constexpr const char* kNsState  = "lampsmart_st";

uint32_t generateDefaultDeviceId() {
  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint32_t id = (static_cast<uint32_t>(mac[2]) << 24) |
                (static_cast<uint32_t>(mac[3]) << 16) |
                (static_cast<uint32_t>(mac[4]) << 8) |
                static_cast<uint32_t>(mac[5]);
  id &= 0x7FFFFFFFu;
  if (id == 0) {
    id = (esp_random() & 0x7FFFFFFFu) | 1u;
  }
  return id;
}
} // namespace

bool ConfigStore::load(DeviceSettings& settings, LampState& state) {
  Preferences prefs;
  if (prefs.begin(kNsConfig, false)) {
    settings.lightName     = prefs.getString("name", "LampSmart Light");
    settings.protocol      = prefs.getString("proto", "v3");
    if (!isValidProtocolId(settings.protocol.c_str())) {
      settings.protocol = "v3";
    }
    settings.deviceType    = prefs.getUShort("dtype", static_cast<uint16_t>(LampDeviceType::LAMP));
    settings.deviceId      = prefs.getUInt("devid", 0);
    if (settings.deviceId == 0) {
      settings.deviceId = generateDefaultDeviceId();
      prefs.putUInt("devid", settings.deviceId);
    }
    settings.channel       = prefs.getUChar("chan", 0);
    settings.deviceAddress = prefs.getUChar("addr", 1);
    settings.paired        = prefs.getBool("paired", false);
    settings.reversedCct   = prefs.getBool("revcct", false);
    settings.adFlag        = prefs.getUChar("adflag", 0x02);
    settings.txCount       = prefs.getUChar("txcnt", 0x10);
    settings.advRepeats    = prefs.getUChar("repeats", 8);
    if (settings.advRepeats < 1 || settings.advRepeats > 32) settings.advRepeats = 8;
    settings.advWindowMs   = prefs.getUShort("winms", 500);
    if (settings.advWindowMs < 50 || settings.advWindowMs > 2000) settings.advWindowMs = 500;
    settings.advEndMarker  = prefs.getBool("endmrk", true);
    settings.mdnsHost      = prefs.getString("mdns", "lampsmart");
    settings.wifiSsid      = prefs.getString("w_ssid", WIFI_DEFAULT_SSID);
    settings.wifiPass      = prefs.getString("w_pass", WIFI_DEFAULT_PASS);
    prefs.end();
  }

  if (prefs.begin(kNsState, true)) {
    state.power       = prefs.getBool("power", false);
    state.brightness  = prefs.getUChar("bright", 80);
    state.temperature = prefs.getUChar("temp", 50);
    state.hue         = prefs.getUShort("hue", 35);
    state.sat         = prefs.getUChar("sat", 35);
    state.val         = prefs.getUChar("val", 100);
    state.r           = prefs.getUChar("r", 255);
    state.g           = prefs.getUChar("g", 214);
    state.b           = prefs.getUChar("b", 170);
    state.colorMode   = prefs.getString("mode", "cct");
    state.scene       = prefs.getUChar("scene", 1);
    prefs.end();
  }
  return true;
}

bool ConfigStore::saveSettings(const DeviceSettings& settings) {
  Preferences prefs;
  if (!prefs.begin(kNsConfig, false)) return false;
  prefs.putString("name", settings.lightName);
  prefs.putString("proto", settings.protocol);
  prefs.putUShort("dtype", settings.deviceType);
  prefs.putUInt("devid", settings.deviceId);
  prefs.putUChar("chan", settings.channel);
  prefs.putUChar("addr", settings.deviceAddress);
  prefs.putBool("paired", settings.paired);
  prefs.putBool("revcct", settings.reversedCct);
  prefs.putUChar("adflag", settings.adFlag);
  prefs.putUChar("txcnt", settings.txCount);
  prefs.putUChar("repeats", settings.advRepeats);
  prefs.putUShort("winms", settings.advWindowMs);
  prefs.putBool("endmrk", settings.advEndMarker);
  prefs.putString("mdns", settings.mdnsHost);
  prefs.end();
  return true;
}

bool ConfigStore::saveTxCount(uint8_t txCount) {
  Preferences prefs;
  if (!prefs.begin(kNsConfig, false)) return false;
  prefs.putUChar("txcnt", txCount);
  prefs.end();
  return true;
}

bool ConfigStore::saveState(const LampState& state) {
  Preferences prefs;
  if (!prefs.begin(kNsState, false)) return false;
  prefs.putBool("power", state.power);
  prefs.putUChar("bright", state.brightness);
  prefs.putUChar("temp", state.temperature);
  prefs.putUShort("hue", state.hue);
  prefs.putUChar("sat", state.sat);
  prefs.putUChar("val", state.val);
  prefs.putUChar("r", state.r);
  prefs.putUChar("g", state.g);
  prefs.putUChar("b", state.b);
  prefs.putString("mode", state.colorMode);
  prefs.putUChar("scene", state.scene);
  prefs.end();
  return true;
}

bool ConfigStore::saveWifi(const String& ssid, const String& pass) {
  Preferences prefs;
  if (!prefs.begin(kNsConfig, false)) return false;
  prefs.putString("w_ssid", ssid);
  prefs.putString("w_pass", pass);
  prefs.end();
  return true;
}

bool ConfigStore::clearWifi() {
  Preferences prefs;
  if (!prefs.begin(kNsConfig, false)) return false;
  prefs.remove("w_ssid");
  prefs.remove("w_pass");
  prefs.end();
  return true;
}

} // namespace lampsmart

