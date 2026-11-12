#include "wifi_manager.h"

#include <WiFi.h>
#include <ESPmDNS.h>

namespace lampsmart {

namespace {
constexpr const char* kTag = "WifiManager";
constexpr const char* kApSsid = "LampSmart-Setup";
constexpr uint32_t kStaConnectTimeoutMs = 15000;
constexpr uint32_t kStaRetryIntervalMs = 30000;
} // namespace

void WifiManager::begin(const DeviceSettings& settings) {
  ssid_ = settings.wifiSsid;
  pass_ = settings.wifiPass;
  mdnsHost_ = settings.mdnsHost.length() ? settings.mdnsHost : "lampsmart";

  WiFi.setHostname(mdnsHost_.c_str());

  if (ssid_.length() == 0) {
    ESP_LOGW(kTag, "No Wi-Fi SSID configured; starting setup AP '%s'", kApSsid);
    startFallbackAp();
  } else {
    startStaConnect();
  }
}

void WifiManager::startStaConnect() {
  dnsServer_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid_.c_str(), pass_.c_str());
  mode_ = WifiModeState::CONNECTING;
  connectStartMs_ = millis();
  mdnsStarted_ = false;
  ESP_LOGI(kTag, "Connecting to Wi-Fi SSID '%s'...", ssid_.c_str());
}

void WifiManager::startFallbackAp() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(kApSsid);
  IPAddress apIp = WiFi.softAPIP();
  dnsServer_.start(53, "*", apIp);
  mode_ = WifiModeState::SETUP_AP;
  lastReconnectAttemptMs_ = millis();
  ESP_LOGI(kTag, "Fallback AP started: SSID='%s' IP=%s", kApSsid, apIp.toString().c_str());
}

void WifiManager::startMdns() {
  if (mdnsStarted_) return;
  if (MDNS.begin(mdnsHost_.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted_ = true;
    ESP_LOGI(kTag, "mDNS active at http://%s.local", mdnsHost_.c_str());
  } else {
    ESP_LOGW(kTag, "Failed to start mDNS responder");
  }
}

bool WifiManager::setCredentialsAndConnect(const String& ssid, const String& pass) {
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    return false;
  }
  ssid_ = ssid;
  pass_ = pass;
  store_.saveWifi(ssid_, pass_);
  startStaConnect();
  return true;
}

void WifiManager::clearCredentialsAndStartAp() {
  ssid_ = "";
  pass_ = "";
  store_.clearWifi();
  startFallbackAp();
}

bool WifiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ipAddress() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  if (mode_ == WifiModeState::SETUP_AP) {
    return WiFi.softAPIP().toString();
  }
  return "0.0.0.0";
}

String WifiManager::ssid() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.SSID();
  }
  if (mode_ == WifiModeState::SETUP_AP) {
    return kApSsid;
  }
  return ssid_;
}

int32_t WifiManager::rssi() const {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
}

void WifiManager::loop() {
  handleSerialConsole();

  if (mode_ == WifiModeState::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      mode_ = WifiModeState::CONNECTED_STA;
      ESP_LOGI(kTag, "Wi-Fi connected! IP=%s", WiFi.localIP().toString().c_str());
      startMdns();
    } else if (millis() - connectStartMs_ > kStaConnectTimeoutMs) {
      ESP_LOGW(kTag, "Wi-Fi connection timed out; starting fallback setup AP");
      startFallbackAp();
    }
  } else if (mode_ == WifiModeState::CONNECTED_STA) {
    if (WiFi.status() != WL_CONNECTED) {
      ESP_LOGW(kTag, "Wi-Fi disconnected; attempting reconnect");
      mode_ = WifiModeState::CONNECTING;
      connectStartMs_ = millis();
      WiFi.reconnect();
    }
  } else if (mode_ == WifiModeState::SETUP_AP) {
    dnsServer_.processNextRequest();
    if (ssid_.length() > 0 && (millis() - lastReconnectAttemptMs_ > kStaRetryIntervalMs)) {
      lastReconnectAttemptMs_ = millis();
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        mode_ = WifiModeState::CONNECTED_STA;
        startMdns();
      } else {
        WiFi.begin(ssid_.c_str(), pass_.c_str());
      }
    }
  }
}

void WifiManager::handleSerialConsole() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c != '\n') {
      if (serialLine_.length() < 160) serialLine_ += c;
      continue;
    }

    serialLine_.trim();
    if (serialLine_.length() > 0) {
      if (serialLine_.startsWith("wifi ")) {
        String rest = serialLine_.substring(5);
        rest.trim();
        int sp = rest.indexOf(' ');
        String newSsid = (sp > 0) ? rest.substring(0, sp) : rest;
        String newPass = (sp > 0) ? rest.substring(sp + 1) : "";
        newSsid.trim();
        newPass.trim();
        if (setCredentialsAndConnect(newSsid, newPass)) {
          Serial.printf("[WiFi] Saved credentials for '%s' and connecting...\n", newSsid.c_str());
        } else {
          Serial.println("[WiFi] Invalid SSID/password length");
        }
      } else if (serialLine_ == "resetwifi") {
        clearCredentialsAndStartAp();
        Serial.println("[WiFi] Cleared saved credentials; Setup AP active.");
      } else if (serialLine_ == "status") {
        Serial.printf("[Status] mode=%d connected=%d ip=%s mdns=http://%s.local\n",
                      static_cast<int>(mode_), isConnected(), ipAddress().c_str(), mdnsHost_.c_str());
      } else if (serialLine_ == "help") {
        Serial.println("Commands: wifi <ssid> <password> | resetwifi | status | help");
      } else {
        Serial.println("Unknown command. Type 'help'.");
      }
    }
    serialLine_ = "";
  }
}

} // namespace lampsmart

