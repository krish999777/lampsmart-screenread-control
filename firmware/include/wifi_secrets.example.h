#pragma once

// Optional compile-time Wi-Fi credentials.
// Copy this file to `wifi_secrets.h` (which is gitignored) if you want
// pre-seeded credentials baked into firmware. Otherwise configure Wi-Fi via:
//   1. USB Serial: `wifi <ssid> <password>`
//   2. Fallback Captive Portal AP: connect to `LampSmart-Setup` -> http://192.168.4.1

#define WIFI_DEFAULT_SSID ""
#define WIFI_DEFAULT_PASS ""

