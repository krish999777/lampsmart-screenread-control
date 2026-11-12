# LampSmart Pro Clone — ESP32 Smart Light Controller

Standalone, local smart-light controller that recreates **LampSmart Pro** using an **ESP32** as the Wi-Fi HTTP server and BLE advertising broadcaster, paired with a React + TypeScript web application served directly from the ESP32's LittleFS flash.

No Home Assistant, no cloud services, and no browser/PC Bluetooth required.

```text
Web App (Phone / Mac) ──HTTP/Wi-Fi──▶ ESP32 (HTTP API + Protocol Encoder + BLE Adv) ──BLE──▶ Lamp
```

## Features

- **Verified LampSmart Pro V3 Protocol** (ported from `qinjunhaocn/xLamp`):
  - 31-byte V3 PDU, AES-128-ECB signature, `XBOXES` whitening, CRC-16/CCITT
  - 13 × 16-bit Service UUIDs (`AD Type 0x03`, matching xLamp / Android) plus selectable `v3_srvdata` (`0x16`), `v2` (unsigned), and legacy `v1a` / `v1b` (CCT-only)
  - `8 × 500 ms` advertising sequence + `[0xBBAA, 0x3412]` end marker (configurable)
- **Full Light Controls**:
  - Power On / Off
  - Brightness `1–100%` (retains 10% protocol floor so `1%` never powers off the lamp)
  - Continuous Warm ↔ Cool Color Temperature (`0–100`)
  - Interactive HSV Color Wheel, RGB inputs, and 12 Color Presets
  - Scenes 1–4, Scene On, Scene Reset, and Night Light mode
- **Guided Pairing & NVS Persistence**:
  - 5-second power-cycle pairing workflow with explicit user confirmation (no fake pairing acks)
  - `txCount`, `deviceId`, `channel`, `deviceAddress`, `protocol`, `paired`, and Wi-Fi credentials persisted in ESP32 NVS
- **Zero-Server Deployment**:
  - React SPA compiles to ~58 KB gzipped and flashes directly onto the ESP32 `LittleFS` partition (`http://lampsmart.local` or ESP32 IP)
  - Automatic `LampSmart-Setup` fallback Wi-Fi captive portal (`http://192.168.4.1`) + USB Serial provisioning

## Project Layout

- `firmware/` — PlatformIO ESP32 C++ firmware + host-runnable protocol unit tests
- `web/` — React + TypeScript + Vite web application
- `scripts/sync_web_to_littlefs.sh` — Builds `web/` and syncs gzipped static assets to `firmware/data/`
- `docs/` — Protocol research, HTTP API spec, architecture, pairing, and troubleshooting guides

## Quick Start

### 1. Build & Flash Firmware
```bash
# Optional: copy firmware/include/wifi_secrets.example.h to firmware/include/wifi_secrets.h
# (or provision via USB Serial / fallback AP after flashing)

# Sync web assets to firmware/data/
./scripts/sync_web_to_littlefs.sh

# Build and flash firmware + LittleFS filesystem (adjust environment if using C3/S3)
cd firmware
pio run -e esp32dev -t upload
pio run -e esp32dev -t uploadfs
```

### 2. Provision Wi-Fi (if not using `wifi_secrets.h`)
- **Option A (Captive Portal):** Connect to Wi-Fi network `LampSmart-Setup`, open `http://192.168.4.1`, enter your home Wi-Fi SSID/password.
- **Option B (USB Serial @ 115200 baud):** Type `wifi YourSSID YourPassword` and press Enter.

### 3. Open in Browser & Pair
1. Open `http://lampsmart.local` (or the IP printed in Serial Monitor) on your Mac or phone.
2. Tap **Pair**, power-cycle your lamp via its wall switch, and tap **Send Pair Command** within 5 seconds.
3. Confirm when the lamp flashes/beeps.

## Automation / External Scripts (`curl`)

```bash
curl -X POST http://lampsmart.local/api/v1/light/brightness \
  -H "Content-Type: application/json" \
  -d '{"brightness":25}'

curl -X POST http://lampsmart.local/api/v1/light/color \
  -H "Content-Type: application/json" \
  -d '{"h":220,"s":70,"v":80}'
```

See [`docs/HTTP_API.md`](docs/HTTP_API.md) for all endpoints.

## Running Automated Tests

```bash
# 1. Host C++ protocol unit tests (verifies Unicorn xLamp vectors, CRC, AES, dimming, RGB, V1)
g++ -std=c++17 -Wall -Wextra -Werror \
  firmware/test/test_protocol.cpp \
  firmware/src/protocols/lamp_protocol.cpp \
  firmware/src/protocols/lampsmart_v3.cpp \
  firmware/src/protocols/legacy_protocol.cpp \
  firmware/src/protocols/aes128.cpp \
  -o /tmp/test_protocol && /tmp/test_protocol && rm -f /tmp/test_protocol

# 2. Web color math & HTTP API client tests
cd web && npm test
```

## License

See [`LICENSE`](LICENSE) for PolyForm Noncommercial License 1.0.0 (V3 protocol derived from `qinjunhaocn/xLamp`) and MIT License notices.

