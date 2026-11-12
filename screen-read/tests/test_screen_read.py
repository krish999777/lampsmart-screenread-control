"""Unit and integration tests for screen-read."""

from __future__ import annotations

import json
import os
import struct
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from brightness_processor import BrightnessProcessor, SyncParameters
from esp32_client import Esp32LightClient
from screen_capture import SimulatedScreenCapturer, parse_bmp_luminance
from server import create_http_server
from sync_service import ScreenSyncService, SyncConfig


def make_bmp(width: int, height: int, bgr: tuple[int, int, int], bpp: int = 24, top_down: bool = False) -> bytes:
    row_raw = bytes([bgr[0], bgr[1], bgr[2]] + ([255] if bpp == 32 else [])) * width
    pad = (4 - (len(row_raw) % 4)) % 4
    row = row_raw + (b"\x00" * pad)
    pixels = row * height
    hdr = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
    dib_h = -height if top_down else height
    dib = struct.pack("<IiiHHIIiiII", 40, width, dib_h, 1, bpp, 0, len(pixels), 2835, 2835, 0, 0)
    return hdr + dib + pixels


class TestScreenRead(unittest.TestCase):
    def test_bmp_luminance_parsing(self) -> None:
        black = parse_bmp_luminance(make_bmp(4, 4, (0, 0, 0)))
        self.assertEqual(black.luminance, 0.0)

        white = parse_bmp_luminance(make_bmp(4, 4, (255, 255, 255), bpp=32, top_down=True))
        self.assertEqual(white.luminance, 100.0)
        self.assertEqual((white.width, white.height), (4, 4))

        green = parse_bmp_luminance(make_bmp(3, 2, (0, 255, 0), bpp=24))
        self.assertAlmostEqual(green.luminance, 71.52, delta=0.1)

    def test_direct_instant_mapping_and_deadband(self) -> None:
        proc = BrightnessProcessor()
        params = SyncParameters(sensitivity=50, min_brightness=10, max_brightness=90)

        # 10% luminance -> 18% room brightness immediately
        s0 = proc.process(10.0, params)
        self.assertTrue(s0.should_send)
        self.assertEqual(s0.output_brightness, 18)

        # Tiny 1% noise is ignored by deadband (deadband=2 at sensitivity=50)
        s1 = proc.process(11.0, params)
        self.assertFalse(s1.should_send)

        # Sudden jump to bright scene (95%) -> jumps directly to 86% immediately (no gradual lag)
        s2 = proc.process(95.0, params)
        self.assertTrue(s2.should_send)
        self.assertEqual(s2.output_brightness, 86)

        # Sudden drop back to 0% -> jumps directly to min_brightness (10%) immediately
        s3 = proc.process(0.0, params)
        self.assertTrue(s3.should_send)
        self.assertEqual(s3.output_brightness, 10)

    def test_sensitivity_validation(self) -> None:
        with self.assertRaises(ValueError):
            SyncParameters(sensitivity=0, min_brightness=10, max_brightness=90).validate()
        with self.assertRaises(ValueError):
            SyncParameters(sensitivity=50, min_brightness=80, max_brightness=20).validate()

    def test_service_and_http_api_with_mock_esp32(self) -> None:
        received_brightness: list[int] = []

        class MockEspHandler(BaseHTTPRequestHandler):
            def log_message(self, format, *args):
                return

            def do_GET(self):
                if self.path == "/api/v1/status":
                    body = json.dumps({"online": True}).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(body)

            def do_POST(self):
                if self.path == "/api/v1/light/brightness":
                    n = int(self.headers.get("Content-Length", "0"))
                    data = json.loads(self.rfile.read(n).decode())
                    received_brightness.append(data["brightness"])
                    body = json.dumps({"success": True, "status": "transmitted"}).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(body)

        esp_srv = HTTPServer(("127.0.0.1", 0), MockEspHandler)
        esp_port = esp_srv.server_address[1]
        esp_thread = threading.Thread(target=esp_srv.serve_forever, daemon=True)
        esp_thread.start()

        with tempfile.TemporaryDirectory() as tmpdir:
            cfg_path = os.path.join(tmpdir, "config.json")
            capturer = SimulatedScreenCapturer([5.0, 95.0, 10.0])
            client = Esp32LightClient(f"http://127.0.0.1:{esp_port}")
            service = ScreenSyncService(
                capturer=capturer,
                client=client,
                config_path=cfg_path,
                initial_config=SyncConfig(
                    esp32_url=f"http://127.0.0.1:{esp_port}",
                    sensitivity=80,
                    min_brightness=10,
                    max_brightness=90,
                    interval_ms=250,
                    dry_run=False,
                ),
            )

            httpd = create_http_server(service, host="127.0.0.1", port=0)
            ui_port = httpd.server_address[1]
            ui_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
            ui_thread.start()

            try:
                base = f"http://127.0.0.1:{ui_port}"
                req = urllib.request.Request(f"{base}/api/sync/start", method="POST", data=b"{}")
                with urllib.request.urlopen(req) as r:
                    res = json.loads(r.read().decode())
                    self.assertTrue(res["active"])

                time.sleep(0.85)

                req = urllib.request.Request(f"{base}/api/sync/stop", method="POST", data=b"{}")
                with urllib.request.urlopen(req) as r:
                    res = json.loads(r.read().decode())
                    self.assertFalse(res["active"])

                cfg_req = urllib.request.Request(
                    f"{base}/api/config",
                    method="POST",
                    headers={"Content-Type": "application/json"},
                    data=json.dumps({"sensitivity": 90, "minBrightness": 15, "maxBrightness": 85}).encode(),
                )
                with urllib.request.urlopen(cfg_req) as r:
                    cfg_res = json.loads(r.read().decode())
                    self.assertEqual(cfg_res["config"]["sensitivity"], 90)

                with urllib.request.urlopen(f"{base}/api/status") as r:
                    st = json.loads(r.read().decode())
                    self.assertFalse(st["active"])
                    self.assertEqual(st["config"]["intervalMs"], 250)
                    self.assertGreaterEqual(st["metrics"]["samplesTaken"], 3)

                # Verify direct jumps [14, 86, 18] were received rapidly without gradual ramp
                self.assertGreaterEqual(len(received_brightness), 3)
                self.assertEqual(received_brightness[0], 14)
                self.assertEqual(received_brightness[1], 86)
                self.assertEqual(received_brightness[2], 18)
            finally:
                service.stop()
                httpd.shutdown()
                httpd.server_close()
                esp_srv.shutdown()
                esp_srv.server_close()


if __name__ == "__main__":
    unittest.main()
