#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include "device_config.h"

namespace lampsmart {

enum class WifiModeState : uint8_t {
  CONNECTING = 0,
  CONNECTED_STA,
  SETUP_AP,
};

class WifiManager {
public:
  void begin(const DeviceSettings& settings);
  void loop();

  bool setCredentialsAndConnect(const String& ssid, const String& pass);
  void clearCredentialsAndStartAp();

  WifiModeState mode() const { return mode_; }
  bool isConnected() const;
  String ipAddress() const;
  String ssid() const;
  int32_t rssi() const;
  String mdnsHost() const { return mdnsHost_; }
  bool isApActive() const { return mode_ == WifiModeState::SETUP_AP; }

private:
  void startStaConnect();
  void startFallbackAp();
  void startMdns();
  void handleSerialConsole();

  ConfigStore store_;
  DNSServer dnsServer_;
  WifiModeState mode_ = WifiModeState::CONNECTING;
  String ssid_;
  String pass_;
  String mdnsHost_ = "lampsmart";
  uint32_t connectStartMs_ = 0;
  uint32_t lastReconnectAttemptMs_ = 0;
  bool mdnsStarted_ = false;
  String serialLine_;
};

} // namespace lampsmart

