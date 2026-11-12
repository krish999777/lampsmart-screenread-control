#pragma once

#include <WebServer.h>
#include "lamp_controller.h"
#include "wifi_manager.h"

namespace lampsmart {

class LampHttpServer {
public:
  LampHttpServer(LampController& controller, WifiManager& wifi);
  void begin();
  void loop();

private:
  void registerRoutes();
  void sendCorsHeaders();
  void sendJson(int code, const String& body);
  void sendApiError(int code, const char* status, const String& message);
  void sendCommandOutcome(const CommandResult& res);

  void handleStatus();
  void handleState();
  void handlePair();
  void handleUnpair();
  void handlePower();
  void handleBrightness();
  void handleColor();
  void handleTemperature();
  void handleScene();
  void handleNight();
  void handleDeviceConfig();
  void handleDeviceTest();
  void handleWifiConfig();
  void handleWifiScan();
  void handleStaticOrFallback();

  LampController& controller_;
  WifiManager& wifi_;
  WebServer server_{80};
  bool fsReady_ = false;
};

} // namespace lampsmart

