# Screen-Read — Instant Screen Brightness Sync (250ms)

Independent service that monitors the **visual brightness** of your computer screen every **250 ms** and directly updates your room light via the existing ESP32 REST API (`POST /api/v1/light/brightness`).

- Completely isolated from `firmware/` and `web/`
- Persistent native macOS ScreenCaptureKit helper (`mac_screen_lum`, compiled automatically via `swiftc`) for ~10 ms in-memory 64×64 luminance capture with zero disk writes (falls back to `screencapture` + `sips` if needed)
- Cached mDNS resolution + persistent HTTP keep-alive connection to the ESP32
- Zero external Python packages required

## Starting & Stopping

```bash
cd screen-read
./start.sh --esp32-url http://lampsmart.local
```

*(Replace `http://lampsmart.local` with your ESP32's direct IP address if desired.)*

Open **http://127.0.0.1:3100** in your browser to:
- **Start / Stop** synchronization at any time (auto-starts on launch unless `--no-autostart` is passed)
- View live **Active / Stopped** status, measured screen brightness %, and sent light brightness %
- Adjust **Sensitivity** (noise deadband `1–5%` vs instant `1%` tracking)
- Set **Min Room Brightness** and **Max Room Brightness** (`1–100%`)
- Switch **ESP32 URL**, **Check Interval** (default `250 ms`), or **Dry-Run** mode

Press `Ctrl+C` in the terminal (or click **Stop Sync**) to stop cleanly without interfering with the main LampSmart Pro web app.

## macOS Screen Recording Permission

Grant **Screen Recording** permission to your terminal app so `mac_screen_lum` / `screencapture` can read screen pixels:

> **System Settings → Privacy & Security → Screen Recording → enable your Terminal app**

## CLI Flags

```text
--esp32-url URL     ESP32 base URL (default: http://lampsmart.local)
--sensitivity 1-100 Responsiveness / deadband threshold (default: 75)
--min 1-100         Minimum room brightness % (default: 10)
--max 1-100         Maximum room brightness % (default: 95)
--interval MS       Screen check interval in ms (default: 250)
--no-autostart      Start control server without auto-starting sync
--dry-run           Measure screen without sending HTTP requests to ESP32
--simulate          Use simulated luminance sequence for testing
```

## Running Tests

```bash
cd screen-read
python3 -m unittest discover -s tests -v
```
