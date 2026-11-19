#include "http_server.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

namespace lampsmart {

namespace {
constexpr const char* kTag = "HttpServer";

const char* contentTypeForPath(const String& path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
  if (path.endsWith(".css"))  return "text/css; charset=utf-8";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "application/octet-stream";
}

const char* kFallbackSetupHtml = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LampSmart ESP32 Setup</title>
<style>
body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:24px;display:flex;justify-content:center}
.card{max-width:420px;width:100%;background:#1e293b;border-radius:16px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,.35)}
h1{margin:0 0 8px;font-size:1.25rem}p{color:#94a3b8;font-size:.9rem;line-height:1.4}
label{display:block;margin:14px 0 6px;font-size:.85rem;color:#cbd5e1}
input,button{width:100%;box-sizing:border-box;padding:11px 12px;border-radius:10px;border:1px solid #334155;background:#0f172a;color:#f8fafc;font-size:.95rem}
button{margin-top:18px;background:#3b82f6;border:none;font-weight:600;cursor:pointer}
.msg{margin-top:12px;font-size:.85rem;color:#38bdf8}
</style></head>
<body><div class="card">
<h1>LampSmart ESP32</h1>
<p>Web assets are not uploaded to LittleFS yet (or device is in Wi-Fi setup mode). Configure Wi-Fi below or upload the LittleFS filesystem image.</p>
<form id="f">
<label>Wi-Fi SSID</label><input id="s" required placeholder="Home Wi-Fi name">
<label>Wi-Fi Password</label><input id="p" type="password" placeholder="Password">
<button type="submit">Save &amp; Connect</button>
</form>
<div class="msg" id="m"></div>
<script>
document.getElementById('f').onsubmit=async(e)=>{e.preventDefault();
const m=document.getElementById('m');m.textContent='Connecting...';
const r=await fetch('/api/v1/wifi/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s.value,password:p.value})});
const j=await r.json();m.textContent=j.message||'Saved';};
</script>
</div></body></html>)rawliteral";

} // namespace

LampHttpServer::LampHttpServer(LampController& controller, WifiManager& wifi)
    : controller_(controller), wifi_(wifi) {}

void LampHttpServer::begin() {
  fsReady_ = LittleFS.begin(false);
  if (!fsReady_) {
    ESP_LOGW(kTag, "LittleFS mount failed (fallback HTML active until filesystem is uploaded)");
  } else {
    ESP_LOGI(kTag, "LittleFS mounted (%u / %u bytes used)", LittleFS.usedBytes(), LittleFS.totalBytes());
  }

  registerRoutes();
  server_.begin();
  ESP_LOGI(kTag, "HTTP server listening on port 80");
}

void LampHttpServer::loop() {
  server_.handleClient();
}

void LampHttpServer::sendCorsHeaders() {
  server_.sendHeader("Access-Control-Allow-Origin", "*");
  server_.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server_.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void LampHttpServer::sendJson(int code, const String& body) {
  sendCorsHeaders();
  server_.send(code, "application/json", body);
}

void LampHttpServer::sendApiError(int code, const char* status, const String& message) {
  JsonDocument doc;
  doc["success"] = false;
  doc["status"] = status;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  sendJson(code, out);
}

void LampHttpServer::sendCommandOutcome(const CommandResult& res) {
  if (res.outcome == EnqueueOutcome::OK) {
    JsonDocument doc;
    doc["success"] = true;
    doc["status"] = res.status;
    doc["message"] = res.message;
    doc["txCount"] = res.txCount;
    doc["frameHex"] = res.frameHex;
    String out;
    serializeJson(doc, out);
    sendJson(200, out);
    return;
  }

  int httpCode = 500;
  if (res.outcome == EnqueueOutcome::UNSUPPORTED_BY_PROTOCOL) httpCode = 400;
  else if (res.outcome == EnqueueOutcome::BLE_UNAVAILABLE) httpCode = 503;
  else if (res.outcome == EnqueueOutcome::QUEUE_TIMEOUT) httpCode = 504;

  sendApiError(httpCode, res.status.c_str(), res.message);
}

void LampHttpServer::registerRoutes() {
  auto handleOptions = [this]() {
    sendCorsHeaders();
    server_.send(204);
  };

  server_.on("/api/v1/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/v1/state", HTTP_GET, [this]() { handleState(); });
  server_.on("/api/v1/pair", HTTP_POST, [this]() { handlePair(); });
  server_.on("/api/v1/unpair", HTTP_POST, [this]() { handleUnpair(); });
  server_.on("/api/v1/light/power", HTTP_POST, [this]() { handlePower(); });
  server_.on("/api/v1/light/brightness", HTTP_POST, [this]() { handleBrightness(); });
  server_.on("/api/v1/light/color", HTTP_POST, [this]() { handleColor(); });
  server_.on("/api/v1/light/temperature", HTTP_POST, [this]() { handleTemperature(); });
  server_.on("/api/v1/light/scene", HTTP_POST, [this]() { handleScene(); });
  server_.on("/api/v1/light/night", HTTP_POST, [this]() { handleNight(); });
  server_.on("/api/v1/device/config", HTTP_POST, [this]() { handleDeviceConfig(); });
  server_.on("/api/v1/device/test", HTTP_POST, [this]() { handleDeviceTest(); });
  server_.on("/api/v1/wifi/config", HTTP_POST, [this]() { handleWifiConfig(); });
  server_.on("/api/v1/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });

  server_.on("/api/v1/status", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/state", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/pair", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/unpair", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/light/power", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/light/brightness", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/light/color", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/light/temperature", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/light/scene", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/light/night", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/device/config", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/device/test", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/wifi/config", HTTP_OPTIONS, handleOptions);
  server_.on("/api/v1/wifi/scan", HTTP_OPTIONS, handleOptions);

  server_.onNotFound([this]() {
    if (server_.method() == HTTP_OPTIONS) {
      sendCorsHeaders();
      server_.send(204);
      return;
    }
    if (server_.uri().startsWith("/api/")) {
      sendApiError(404, "not_found", "API endpoint not found");
      return;
    }
    handleStaticOrFallback();
  });
}

void LampHttpServer::handleStatus() {
  const DeviceSettings cfg = controller_.getSettings();
  const TransmissionStatus tx = controller_.getTransmissionStatus();
  const LampProtocol* proto = controller_.getActiveProtocol();

  JsonDocument doc;
  doc["online"] = true;
  doc["uptimeMs"] = millis();
  doc["wifi"]["connected"] = wifi_.isConnected();
  doc["wifi"]["apMode"] = wifi_.isApActive();
  doc["wifi"]["ssid"] = wifi_.ssid();
  doc["wifi"]["ip"] = wifi_.ipAddress();
  doc["wifi"]["rssi"] = wifi_.rssi();
  doc["wifi"]["mdns"] = String("http://") + wifi_.mdnsHost() + ".local";

  doc["ble"]["initialized"] = controller_.isBleReady();
  doc["ble"]["error"] = controller_.bleError();

  doc["protocol"]["id"] = proto->id();
  doc["protocol"]["name"] = proto->displayName();
  doc["protocol"]["supportsRgb"] = proto->supportsRgb();
  doc["protocol"]["supportsScenes"] = proto->supportsScenes();

  doc["device"]["lightName"] = cfg.lightName;
  doc["device"]["deviceType"] = cfg.deviceType;
  doc["device"]["deviceId"] = cfg.deviceId;
  doc["device"]["channel"] = cfg.channel;
  doc["device"]["deviceAddress"] = cfg.deviceAddress;
  doc["device"]["var1"] = (cfg.deviceId + cfg.channel) & 0x7FFFFFFFu;
  doc["device"]["paired"] = cfg.paired;
  doc["device"]["reversedCct"] = cfg.reversedCct;
  doc["device"]["adFlag"] = cfg.adFlag;
  doc["device"]["advRepeats"] = cfg.advRepeats;
  doc["device"]["advWindowMs"] = cfg.advWindowMs;
  doc["device"]["advEndMarker"] = cfg.advEndMarker;
  doc["device"]["mdnsHost"] = cfg.mdnsHost;

  doc["lastTransmission"]["success"] = tx.success;
  doc["lastTransmission"]["status"] = tx.status;
  doc["lastTransmission"]["message"] = tx.message;
  doc["lastTransmission"]["command"] = tx.command;
  doc["lastTransmission"]["protocol"] = tx.protocol;
  doc["lastTransmission"]["txCount"] = tx.txCount;
  doc["lastTransmission"]["timestampMs"] = tx.timestampMs;
  doc["lastTransmission"]["frameHex"] = tx.frameHex;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void LampHttpServer::handleState() {
  const LampState st = controller_.getState();
  const LampProtocol* proto = controller_.getActiveProtocol();

  JsonDocument doc;
  doc["pushOnly"] = true;
  doc["note"] = "State reflects last locally requested/transmitted command (BLE advertising is one-way).";
  doc["power"] = st.power;
  doc["brightness"] = st.brightness;
  doc["temperature"] = st.temperature;
  doc["colorMode"] = st.colorMode;
  doc["supportsRgb"] = proto->supportsRgb();
  doc["color"]["h"] = st.hue;
  doc["color"]["s"] = st.sat;
  doc["color"]["v"] = st.val;
  doc["color"]["r"] = st.r;
  doc["color"]["g"] = st.g;
  doc["color"]["b"] = st.b;
  doc["scene"] = st.scene;
  doc["lastCommandTimestampMs"] = st.lastCommandMs;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void LampHttpServer::handlePair() {
  LampCommand cmd;
  cmd.type = LampCommandType::PAIR;
  const CommandResult res = controller_.executeCommand(cmd, "pair");
  sendCommandOutcome(res);
}

void LampHttpServer::handleUnpair() {
  LampCommand cmd;
  cmd.type = LampCommandType::UNPAIR;
  const CommandResult res = controller_.executeCommand(cmd, "unpair");
  sendCommandOutcome(res);
}

void LampHttpServer::handlePower() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok || !doc["on"].is<bool>()) {
    sendApiError(400, "invalid_request", "Expected JSON body with boolean 'on'");
    return;
  }
  LampCommand cmd;
  cmd.type = LampCommandType::POWER;
  cmd.on = doc["on"].as<bool>();
  const CommandResult res = controller_.executeCommand(cmd, cmd.on ? "power_on" : "power_off");
  sendCommandOutcome(res);
}

void LampHttpServer::handleBrightness() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok || !doc["brightness"].is<int>()) {
    sendApiError(400, "invalid_request", "Expected JSON body with integer 'brightness' (1..100)");
    return;
  }
  const int b = doc["brightness"].as<int>();
  if (b < 1 || b > 100) {
    sendApiError(400, "invalid_brightness", "brightness must be between 1 and 100");
    return;
  }

  const LampState st = controller_.getState();
  LampCommand cmd;
  cmd.type = LampCommandType::DIMMING;
  cmd.brightness = static_cast<uint8_t>(b);
  cmd.temperature = st.temperature;
  const CommandResult res = controller_.executeCommand(cmd, "brightness", true);
  sendCommandOutcome(res);
}

void LampHttpServer::handleTemperature() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok || !doc["temperature"].is<int>()) {
    sendApiError(400, "invalid_request", "Expected JSON body with integer 'temperature' (0..100)");
    return;
  }
  const int t = doc["temperature"].as<int>();
  if (t < 0 || t > 100) {
    sendApiError(400, "invalid_temperature", "temperature must be between 0 (warm) and 100 (cool)");
    return;
  }

  const LampState st = controller_.getState();
  LampCommand cmd;
  cmd.type = LampCommandType::DIMMING;
  cmd.brightness = st.brightness;
  cmd.temperature = static_cast<uint8_t>(t);
  const CommandResult res = controller_.executeCommand(cmd, "temperature", true);
  sendCommandOutcome(res);
}

void LampHttpServer::handleColor() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
    sendApiError(400, "invalid_json", "Invalid JSON body");
    return;
  }

  uint8_t r = 0, g = 0, b = 0;
  uint8_t bright = 100;

  if (doc["h"].is<float>() && doc["s"].is<float>() && doc["v"].is<float>()) {
    const float h = doc["h"].as<float>();
    const float s = doc["s"].as<float>();
    const float v = doc["v"].as<float>();
    if (h < 0.0f || h > 360.0f || s < 0.0f || s > 100.0f || v < 0.0f || v > 100.0f) {
      sendApiError(400, "invalid_hsv", "HSV values must be h: 0..360, s: 0..100, v: 0..100");
      return;
    }
    // Convert full-value hue/saturation to RGB, then scale via LampCommands.scaleChannel with v
    hsvToRgb(h, s, 100.0f, r, g, b);
    bright = static_cast<uint8_t>(v < 1.0f ? 1 : v);
  } else if (doc["r"].is<int>() && doc["g"].is<int>() && doc["b"].is<int>()) {
    const int ri = doc["r"].as<int>();
    const int gi = doc["g"].as<int>();
    const int bi = doc["b"].as<int>();
    if (ri < 0 || ri > 255 || gi < 0 || gi > 255 || bi < 0 || bi > 255) {
      sendApiError(400, "invalid_rgb", "RGB values must be between 0 and 255");
      return;
    }
    r = static_cast<uint8_t>(ri);
    g = static_cast<uint8_t>(gi);
    b = static_cast<uint8_t>(bi);
    if (doc["brightness"].is<int>()) {
      const int br = doc["brightness"].as<int>();
      if (br < 1 || br > 100) {
        sendApiError(400, "invalid_brightness", "brightness must be between 1 and 100");
        return;
      }
      bright = static_cast<uint8_t>(br);
    }
  } else {
    sendApiError(400, "invalid_request", "Expected {h, s, v} or {r, g, b}");
    return;
  }

  LampCommand cmd;
  cmd.type = LampCommandType::RGB;
  cmd.r = r;
  cmd.g = g;
  cmd.b = b;
  cmd.brightness = bright;
  const CommandResult res = controller_.executeCommand(cmd, "color", true);
  sendCommandOutcome(res);
}

void LampHttpServer::handleScene() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
    sendApiError(400, "invalid_json", "Invalid JSON body");
    return;
  }

  LampCommand cmd;
  String label = "scene";

  if (doc["action"].is<const char*>()) {
    const String action = doc["action"].as<const char*>();
    if (action == "on") {
      cmd.type = LampCommandType::SCENE_ON;
      label = "scene_on";
    } else if (action == "reset") {
      cmd.type = LampCommandType::SCENE_RESET;
      label = "scene_reset";
    } else if (action == "set" && doc["scene"].is<int>()) {
      const int sc = doc["scene"].as<int>();
      if (sc < 1 || sc > 4) {
        sendApiError(400, "invalid_scene", "scene must be between 1 and 4");
        return;
      }
      cmd.type = LampCommandType::SCENE_SET;
      cmd.sceneIndex = static_cast<uint8_t>(sc - 1);
      label = String("scene_set_") + sc;
    } else {
      sendApiError(400, "invalid_request", "Unsupported scene action");
      return;
    }
  } else if (doc["scene"].is<int>()) {
    const int sc = doc["scene"].as<int>();
    if (sc < 1 || sc > 4) {
      sendApiError(400, "invalid_scene", "scene must be between 1 and 4");
      return;
    }
    cmd.type = LampCommandType::SCENE_SWITCH;
    cmd.sceneIndex = static_cast<uint8_t>(sc - 1);
    label = String("scene_") + sc;
  } else {
    sendApiError(400, "invalid_request", "Expected {scene: 1..4} or {action: 'on'|'reset'|'set'}");
    return;
  }

  const CommandResult res = controller_.executeCommand(cmd, label);
  sendCommandOutcome(res);
}

void LampHttpServer::handleNight() {
  LampCommand cmd;
  cmd.type = LampCommandType::NIGHT_LIGHT;
  const CommandResult res = controller_.executeCommand(cmd, "night_light");
  sendCommandOutcome(res);
}

void LampHttpServer::handleDeviceConfig() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
    sendApiError(400, "invalid_json", "Invalid JSON body");
    return;
  }

  DeviceSettings cfg = controller_.getSettings();
  if (doc["lightName"].is<const char*>()) cfg.lightName = doc["lightName"].as<const char*>();
  if (doc["protocol"].is<const char*>()) cfg.protocol = doc["protocol"].as<const char*>();
  if (doc["deviceType"].is<uint16_t>()) cfg.deviceType = doc["deviceType"].as<uint16_t>();
  if (doc["deviceId"].is<uint32_t>()) cfg.deviceId = doc["deviceId"].as<uint32_t>();
  if (doc["channel"].is<int>()) {
    const int ch = doc["channel"].as<int>();
    if (ch < 0 || ch > 15) {
      sendApiError(400, "invalid_channel", "channel must be between 0 and 15");
      return;
    }
    cfg.channel = static_cast<uint8_t>(ch);
  }
  if (doc["deviceAddress"].is<int>()) {
    const int addr = doc["deviceAddress"].as<int>();
    if (addr < 0 || addr > 255) {
      sendApiError(400, "invalid_address", "deviceAddress must be between 0 and 255");
      return;
    }
    cfg.deviceAddress = static_cast<uint8_t>(addr);
  }
  if (doc["paired"].is<bool>()) cfg.paired = doc["paired"].as<bool>();
  if (doc["reversedCct"].is<bool>()) cfg.reversedCct = doc["reversedCct"].as<bool>();
  if (doc["adFlag"].is<int>()) {
    const int f = doc["adFlag"].as<int>();
    if (f < 0 || f > 255) {
      sendApiError(400, "invalid_ad_flag", "adFlag must be 0..255");
      return;
    }
    cfg.adFlag = static_cast<uint8_t>(f);
  }
  if (doc["advRepeats"].is<int>()) {
    const int r = doc["advRepeats"].as<int>();
    if (r < 1 || r > 32) {
      sendApiError(400, "invalid_repeats", "advRepeats must be 1..32");
      return;
    }
    cfg.advRepeats = static_cast<uint8_t>(r);
  }
  if (doc["advWindowMs"].is<int>()) {
    const int w = doc["advWindowMs"].as<int>();
    if (w < 50 || w > 2000) {
      sendApiError(400, "invalid_window", "advWindowMs must be 50..2000");
      return;
    }
    cfg.advWindowMs = static_cast<uint16_t>(w);
  }
  if (doc["advEndMarker"].is<bool>()) cfg.advEndMarker = doc["advEndMarker"].as<bool>();
  if (doc["mdnsHost"].is<const char*>()) cfg.mdnsHost = doc["mdnsHost"].as<const char*>();

  String err;
  if (!controller_.updateSettings(cfg, err)) {
    sendApiError(400, "config_error", err);
    return;
  }

  JsonDocument outDoc;
  outDoc["success"] = true;
  outDoc["status"] = "saved";
  outDoc["message"] = "Device configuration saved";
  String out;
  serializeJson(outDoc, out);
  sendJson(200, out);
}

void LampHttpServer::handleDeviceTest() {
  JsonDocument doc;
  LampCommand cmd;
  String label = "test_power_on";
  if (server_.arg("plain").length() > 0 && deserializeJson(doc, server_.arg("plain")) == DeserializationError::Ok &&
      doc["rawCommand"].is<uint16_t>()) {
    cmd.type = LampCommandType::RAW;
    cmd.rawCommand = doc["rawCommand"].as<uint16_t>();
    cmd.rawP21 = doc["p21"] | 0;
    cmd.rawP22 = doc["p22"] | 0;
    cmd.rawP23 = doc["p23"] | 0;
    label = String("raw_0x") + String(cmd.rawCommand, HEX);
  } else {
    cmd.type = LampCommandType::POWER;
    cmd.on = true;
  }
  const CommandResult res = controller_.executeCommand(cmd, label);
  sendCommandOutcome(res);
}

void LampHttpServer::handleWifiConfig() {
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok || !doc["ssid"].is<const char*>()) {
    sendApiError(400, "invalid_request", "Expected JSON body with 'ssid' and optional 'password'");
    return;
  }
  const String ssid = doc["ssid"].as<const char*>();
  const String pass = doc["password"] | "";
  if (!wifi_.setCredentialsAndConnect(ssid, pass)) {
    sendApiError(400, "invalid_wifi", "Invalid SSID or password length");
    return;
  }

  JsonDocument outDoc;
  outDoc["success"] = true;
  outDoc["status"] = "connecting";
  outDoc["message"] = String("Credentials saved; connecting to '") + ssid + "'";
  String out;
  serializeJson(outDoc, out);
  sendJson(200, out);
}

void LampHttpServer::handleWifiScan() {
  const int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray nets = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    JsonObject obj = nets.add<JsonObject>();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
    obj["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void LampHttpServer::handleStaticOrFallback() {
  if (!fsReady_) {
    sendCorsHeaders();
    server_.send(200, "text/html; charset=utf-8", kFallbackSetupHtml);
    return;
  }

  String path = server_.uri();
  if (path == "/" || path.isEmpty()) {
    path = "/index.html";
  }

  // Check pre-compressed .gz asset first
  String gzPath = path + ".gz";
  if (LittleFS.exists(gzPath)) {
    File f = LittleFS.open(gzPath, "r");
    if (f) {
      sendCorsHeaders();
      server_.streamFile(f, contentTypeForPath(path));
      f.close();
      return;
    }
  }

  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path, "r");
    if (f) {
      sendCorsHeaders();
      server_.streamFile(f, contentTypeForPath(path));
      f.close();
      return;
    }
  }

  // SPA fallback to index.html(.gz)
  if (LittleFS.exists("/index.html.gz")) {
    File f = LittleFS.open("/index.html.gz", "r");
    if (f) {
      sendCorsHeaders();
      server_.streamFile(f, "text/html; charset=utf-8");
      f.close();
      return;
    }
  }
  if (LittleFS.exists("/index.html")) {
    File f = LittleFS.open("/index.html", "r");
    if (f) {
      sendCorsHeaders();
      server_.streamFile(f, "text/html; charset=utf-8");
      f.close();
      return;
    }
  }

  sendCorsHeaders();
  server_.send(200, "text/html; charset=utf-8", kFallbackSetupHtml);
}

} // namespace lampsmart

