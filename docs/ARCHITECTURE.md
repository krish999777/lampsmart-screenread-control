# System Architecture

## Overview

```text
Browser (Mac / Phone)
  │  HTTP REST (same-origin from ESP32 LittleFS or standalone Vite)
  ▼
ESP32 Wi-Fi + WebServer (:80, mDNS lampsmart.local)
  │  Validates JSON, updates requested state, enqueues command
  ▼
LampController (FreeRTOS Coalescing Queue + NVS Persistence)
  │  Latest-value-wins for sliders, preempts obsolete bursts
  ▼
LampProtocol (LampSmartV3Protocol / LampLegacyProtocol)
  │  Builds 31-byte frame (AES-128 + XBOXES + CRC-16/CCITT)
  ▼
BleAdvertiser (ESP-IDF Bluedroid GAP Raw Advertising)
  │  8 × 500ms windows @ 20ms interval + [0xBBAA, 0x3412] end marker
  ▼
LampSmart Pro Fixture (BLE Passive Scanner)
```

## Key Design Decisions

1. **No Home Assistant, No Cloud, No PC Bluetooth:**
   - The React SPA communicates strictly over HTTP with the ESP32.
   - All protocol encoding and BLE broadcasting happens on the ESP32 using Bluedroid GAP raw advertising (`esp_ble_gap_config_adv_data_raw` + `esp_ble_gap_start_advertising`).
2. **Non-Blocking Web Server + Real BLE Feedback:**
   - Long BLE advertising bursts (default `8 × 500 ms + 500 ms` marker = `4.5 s`) execute inside a dedicated FreeRTOS task (`lamp_ble_tx`), never on the HTTP server loop.
   - HTTP handlers synchronize via a lightweight `CommandSync` semaphore only until the ESP32 BLE GAP stack confirms the first advertising packet started (`ESP_BT_STATUS_SUCCESS`). If BLE fails, the API returns HTTP `500`/`503` instead of faking success.
3. **Coalescing & Preemption for Responsive Sliders:**
   - Pending `DIMMING`, `RGB`, and `SCENE_SWITCH` updates in the queue are replaced by newer arrivals (latest-value-wins).
   - Active multi-window bursts honor preemption after an 80 ms minimum floor so dragging a slider stays smooth while the final resting value always gets the full burst.
4. **Embedded Frontend via LittleFS:**
   - The compiled React + Vite bundle (~58 KB gzipped) lives in `firmware/data/` and is flashed to the ESP32 `LittleFS` partition, served directly at `http://lampsmart.local` or the ESP32's IP with zero CORS overhead.
5. **Fallback Wi-Fi Provisioning:**
   - If Wi-Fi credentials are unset or fail to connect within 15 seconds, the ESP32 opens a captive-portal SoftAP (`LampSmart-Setup` at `http://192.168.4.1`). USB Serial provisioning (`wifi <ssid> <password>`) is also built in.

