#include <Arduino.h>

#include "lamp_controller.h"
#include "wifi_manager.h"
#include "http_server.h"

namespace {
lampsmart::LampController gController;
lampsmart::WifiManager gWifi;
lampsmart::LampHttpServer gHttp(gController, gWifi);
} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== LampSmart Pro ESP32 Controller ===");

  gController.begin();
  gWifi.begin(gController.getSettings());
  gHttp.begin();

  Serial.println("Serial commands: wifi <ssid> <pass> | resetwifi | status | help");
}

void loop() {
  gWifi.loop();
  gHttp.loop();
  delay(2);
}

