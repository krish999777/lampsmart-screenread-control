# ESP32 HTTP REST API Reference

Base URL when served from ESP32: relative `/` (or `http://lampsmart.local` / `http://<esp32-ip>`).
All `POST` endpoints accept `Content-Type: application/json` and return JSON. CORS (`*`) is enabled on all `/api/v1/*` routes for local script and standalone UI access.

## Endpoints

### `GET /api/v1/status`
Returns ESP32 online status, Wi-Fi info, BLE stack health, active protocol capabilities, device config, and last BLE transmission metadata.

### `GET /api/v1/state`
Returns the **most recently locally requested/transmitted** lamp state (`pushOnly: true`, because BLE advertising is one-way).

### `POST /api/v1/pair`
Broadcasts the pairing opcode (`0x28`) for the configured `deviceId`, `channel`, and `deviceAddress`. Must be called within ~5 seconds of physically power-cycling the lamp.

### `POST /api/v1/unpair`
Broadcasts the unpair opcode (`0x45`) and clears the local `paired` flag.

### `POST /api/v1/light/power`
```json
{ "on": true }
```

### `POST /api/v1/light/brightness`
`brightness`: integer `1..100` (1% retains a 10% protocol floor so minimum brightness does not power off the lamp).
```json
{ "brightness": 45 }
```

### `POST /api/v1/light/temperature`
`temperature`: integer `0..100` (`0` = warmest white, `50` = neutral, `100` = coolest white).
```json
{ "temperature": 65 }
```

### `POST /api/v1/light/color`
Accepts HSV (`h: 0..360`, `s: 0..100`, `v: 0..100`) or RGB (`r, g, b: 0..255` with optional `brightness: 1..100`). Returns `400` if a CCT-only legacy protocol (`v1a`/`v1b`) is active.
```json
{ "h": 210, "s": 80, "v": 75 }
```

### `POST /api/v1/light/scene`
Select Scene 1–4 or trigger scene actions (`"on"` / `"reset"` / `"set"`):
```json
{ "scene": 2 }
```
Or:
```json
{ "action": "on" }
```

### `POST /api/v1/light/night`
Activates night-light mode (`0x23`).

### `POST /api/v1/device/config`
Updates and persists NVS configuration:
`lightName`, `protocol` (`v3` | `v3_srvdata` | `v2` | `v1a` | `v1b`), `deviceType` (`256` | `512` | `1024` | `2048`), `deviceId`, `channel` (`0..15`), `deviceAddress` (`0..255`), `paired`, `reversedCct`, `adFlag`, `advRepeats` (`1..32`), `advWindowMs` (`50..2000`), `advEndMarker`, `mdnsHost`.

### `POST /api/v1/device/test`
Sends a test Power On command (`{}`) or an explicit raw V3 command (`{"rawCommand": 16, "p21": 0, "p22": 0, "p23": 0}`).

### `POST /api/v1/wifi/config` & `GET /api/v1/wifi/scan`
Updates NVS Wi-Fi credentials (`{"ssid": "...", "password": "..."}`) and reconnects in station mode, or scans visible Wi-Fi networks.

## Example Response & Error Codes

Success (`200 OK`):
```json
{
  "success": true,
  "status": "transmitted",
  "message": "BLE advertising started",
  "txCount": 24,
  "frameHex": "0201021B03F0083080..."
}
```

Errors:
- `400 Bad Request`: invalid parameters or command unsupported by selected legacy protocol.
- `500 Internal Server Error` / `503 Service Unavailable`: ESP32 BLE stack failed to initialize or GAP advertising failed.
- `504 Gateway Timeout`: BLE worker queue timed out.

