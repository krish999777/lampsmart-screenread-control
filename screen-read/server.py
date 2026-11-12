"""Local HTTP control server and CLI entrypoint for screen-read."""

from __future__ import annotations

import argparse
import json
import os
import signal
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

from esp32_client import Esp32LightClient
from screen_capture import MacOsScreenCapturer, SimulatedScreenCapturer
from sync_service import ScreenSyncService, SyncConfig

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
STATIC_INDEX = os.path.join(BASE_DIR, "static", "index.html")
DEFAULT_CONFIG_PATH = os.path.join(BASE_DIR, "config.json")


def create_http_server(
    service: ScreenSyncService,
    host: str = "127.0.0.1",
    port: int = 3100,
) -> ThreadingHTTPServer:
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: Any) -> None:  # noqa: A002
            return  # keep terminal output clean

        def _send_json(self, code: int, payload: dict[str, Any]) -> None:
            raw = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()
            self.wfile.write(raw)

        def _read_json_body(self) -> dict[str, Any]:
            length = int(self.headers.get("Content-Length", "0") or "0")
            if length == 0:
                return {}
            raw = self.rfile.read(length).decode("utf-8")
            parsed = json.loads(raw)
            if not isinstance(parsed, dict):
                raise ValueError("JSON body must be an object")
            return parsed

        def do_OPTIONS(self) -> None:  # noqa: N802
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()

        def do_GET(self) -> None:  # noqa: N802
            if self.path == "/" or self.path == "/index.html":
                if not os.path.exists(STATIC_INDEX):
                    self._send_json(404, {"error": "UI file missing"})
                    return
                with open(STATIC_INDEX, "rb") as f:
                    body = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            if self.path == "/api/status":
                self._send_json(200, service.get_status())
                return

            if self.path == "/api/esp32/ping":
                self._send_json(200, service.check_esp32_health())
                return

            self._send_json(404, {"error": "Not found"})

        def do_POST(self) -> None:  # noqa: N802
            if self.path == "/api/sync/start":
                started = service.start()
                self._send_json(200, {
                    "ok": True,
                    "started": started,
                    "active": service.is_active,
                    "status": service.get_status(),
                })
                return

            if self.path == "/api/sync/stop":
                stopped = service.stop()
                self._send_json(200, {
                    "ok": True,
                    "stopped": stopped,
                    "active": service.is_active,
                    "status": service.get_status(),
                })
                return

            if self.path == "/api/config":
                try:
                    patch = self._read_json_body()
                    cfg = service.update_config(patch)
                except (ValueError, json.JSONDecodeError) as err:
                    self._send_json(400, {"ok": False, "error": str(err)})
                    return
                self._send_json(200, {
                    "ok": True,
                    "config": {
                        "esp32Url": cfg.esp32_url,
                        "sensitivity": cfg.sensitivity,
                        "minBrightness": cfg.min_brightness,
                        "maxBrightness": cfg.max_brightness,
                        "intervalMs": cfg.interval_ms,
                        "dryRun": cfg.dry_run,
                    },
                })
                return

            self._send_json(404, {"error": "Not found"})

    return ThreadingHTTPServer((host, port), Handler)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Screen-Based Automatic Light Brightness Control")
    parser.add_argument("--host", default="127.0.0.1", help="Local bind host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=3100, help="Control UI/API port (default: 3100)")
    parser.add_argument("--esp32-url", default=os.environ.get("ESP32_URL", ""), help="ESP32 base URL (e.g. http://lampsmart.local)")
    parser.add_argument("--sensitivity", type=int, default=None, help="Sync sensitivity 1..100")
    parser.add_argument("--min", dest="min_brightness", type=int, default=None, help="Minimum room brightness 1..100")
    parser.add_argument("--max", dest="max_brightness", type=int, default=None, help="Maximum room brightness 1..100")
    parser.add_argument("--interval", dest="interval_ms", type=int, default=None, help="Polling interval in ms (default: 1000)")
    parser.add_argument("--display", type=int, default=1, help="macOS display index (default: 1)")
    parser.add_argument("--no-autostart", action="store_true", help="Start server paused instead of immediately syncing")
    parser.add_argument("--dry-run", action="store_true", help="Measure screen brightness without sending HTTP calls to ESP32")
    parser.add_argument("--simulate", action="store_true", help="Use simulated screen luminance sequence for demo/testing")
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    capturer = SimulatedScreenCapturer() if args.simulate else MacOsScreenCapturer(display_index=args.display)
    service = ScreenSyncService(capturer=capturer, config_path=DEFAULT_CONFIG_PATH)

    patch: dict[str, Any] = {}
    if args.esp32_url:
        patch["esp32Url"] = args.esp32_url
    if args.sensitivity is not None:
        patch["sensitivity"] = args.sensitivity
    if args.min_brightness is not None:
        patch["minBrightness"] = args.min_brightness
    if args.max_brightness is not None:
        patch["maxBrightness"] = args.max_brightness
    if args.interval_ms is not None:
        patch["intervalMs"] = args.interval_ms
    if args.dry_run:
        patch["dryRun"] = True

    if patch:
        try:
            service.update_config(patch)
        except ValueError as err:
            print(f"Configuration error: {err}", file=sys.stderr)
            return 2

    httpd = create_http_server(service, host=args.host, port=args.port)

    if not args.no_autostart:
        service.start()

    print(f"Screen-Read Sync Server running at http://{args.host}:{args.port}")
    print(f"ESP32 target: {service.config.esp32_url} | Auto-sync: {'ACTIVE' if service.is_active else 'PAUSED'}")
    print("Press Ctrl+C to stop.")

    def shutdown_handler(_signum: int, _frame: Any) -> None:
        print("\nStopping screen-read service...")
        service.stop()
        threading_shutdown = lambda: httpd.shutdown()
        import threading
        threading.Thread(target=threading_shutdown, daemon=True).start()

    signal.signal(signal.SIGINT, shutdown_handler)
    signal.signal(signal.SIGTERM, shutdown_handler)

    try:
        httpd.serve_forever()
    finally:
        service.stop()
        httpd.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

