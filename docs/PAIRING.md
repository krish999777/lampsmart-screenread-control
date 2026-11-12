# Pairing & Unpairing Guide

LampSmart Pro fixtures use passive BLE advertising scanning and only accept a new controller identity (`deviceId` + `channel` + `deviceAddress`) during a short **~5-second window** immediately after mains power-on.

## Guided Pairing Steps

1. Open the web app (`http://lampsmart.local` or the ESP32's IP address).
2. Click **Pair** (in the top banner or **Settings → Pair Light Now**).
3. Switch the lamp **OFF** at its physical wall switch/breaker for at least 3–5 seconds.
4. Switch the lamp **ON** and immediately click **Send Pair Command** within 5 seconds.
5. Watch the lamp for a **flash, blink, or audible beep** confirming it bound to the ESP32's ID.
6. Only if the lamp reacted, click **Yes, Lamp Flashed — Confirm Paired** to persist `paired: true` in ESP32 NVS.

## Unpairing

- Open the Pairing modal (when paired) and click **Unpair Lamp** (`POST /api/v1/unpair`), or power-cycle the lamp and trigger unpair within 5 seconds if your fixture requires power-cycle unpairing.
- Note: if the lamp was previously paired to another phone/remote and refuses new pairing, unpair it in the original app first or switch **Protocol Version** in **Settings** (`v3` → `v3_srvdata` → `v2` → `v1a` → `v1b`) to match your lamp's generation.

