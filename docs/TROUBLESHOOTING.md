# Troubleshooting & Hardware Test Checklist

## Hardware Test Procedure (Real LampSmart Pro Fixture)

1. Flash firmware: `cd firmware && pio run -e esp32dev -t upload`
2. Upload web app to LittleFS: `./scripts/sync_web_to_littlefs.sh && cd firmware && pio run -e esp32dev -t uploadfs`
3. Provision Wi-Fi via USB serial (`wifi MySSID MyPassword`) or connect to `LampSmart-Setup` AP (`http://192.168.4.1`).
4. Open `http://lampsmart.local` (or the printed IP in Serial Monitor) in a browser on your Mac or phone.
5. Power-cycle the physical lamp and complete the **Guided Pairing** flow within 5 seconds.
6. Test **Power On / Off** from the main orb button.
7. Test **Brightness** slider (including `Min 1%` and `Max 100%`) and confirm `1%` dims without turning the lamp off.
8. Test **Color Temperature** slider (`Warm 0` ↔ `Cool 100`). If warm/cool are backwards, enable **Reverse Warm / Cool white channels** in Settings.
9. Test **Color (RGB)** wheel, RGB inputs, and 12 presets (for RGB-capable V3 fixtures).
10. Test **Scenes 1–4**, **Scene On**, and **Night Light**.
11. Power-cycle/reboot the ESP32 and verify `GET /api/v1/status` retains `paired: true`, `deviceId`, `protocol`, and Wi-Fi credentials.

## Common Issues

- **Lamp does not react during pairing:**
  - Verify the ESP32 is within Bluetooth range of the fixture (same room recommended).
  - Power-cycle the lamp OFF for at least 5 seconds, turn ON, and tap **Send Pair Command** within 5 seconds.
  - Try switching **Protocol Version** in Settings (`v3` → `v3_srvdata` → `v2` → `v1a` → `v1b`). Different LampSmart Pro fixture batches use different protocol generations.
- **Warm and Cool are swapped:**
  - Enable **Reverse Warm / Cool white channels** in Settings and save.
- **RGB doesn't change color on my lamp:**
  - Confirm your lamp hardware has RGB LEDs (not CCT-only). If your lamp uses `v1a`/`v1b`, those legacy protocols are CCT-only.
- **`lampsmart.local` doesn't resolve:**
  - Use the IP address printed in Serial Monitor (`115200` baud) or check your router's DHCP client list. Some Android devices do not resolve `.local` mDNS reliably and require the direct IP.
- **Fallback HTML page appears instead of React UI:**
  - Run `./scripts/sync_web_to_littlefs.sh` and `pio run -e esp32dev -t uploadfs` to flash the `LittleFS` partition.

