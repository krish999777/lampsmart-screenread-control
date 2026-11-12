"""Background screen-to-light brightness synchronization service."""

from __future__ import annotations

import json
import os
import threading
import time
from collections import deque
from dataclasses import asdict, dataclass
from typing import Any

from brightness_processor import BrightnessProcessor, SyncParameters
from esp32_client import Esp32LightClient
from screen_capture import ScreenCapturer


@dataclass
class SyncConfig:
    esp32_url: str = "http://lampsmart.local"
    sensitivity: int = 75
    min_brightness: int = 10
    max_brightness: int = 95
    interval_ms: int = 250
    dry_run: bool = False

    def validate(self) -> None:
        SyncParameters(
            sensitivity=self.sensitivity,
            min_brightness=self.min_brightness,
            max_brightness=self.max_brightness,
        ).validate()
        if not (100 <= self.interval_ms <= 10000):
            raise ValueError("interval_ms must be between 100 and 10000")
        if not self.esp32_url.startswith(("http://", "https://")):
            raise ValueError("esp32_url must start with http:// or https://")


class ScreenSyncService:
    """Continuously monitors screen luminance and adjusts the ESP32 light smoothly."""

    def __init__(
        self,
        capturer: ScreenCapturer,
        client: Esp32LightClient | None = None,
        config_path: str | None = None,
        initial_config: SyncConfig | None = None,
    ) -> None:
        self.capturer = capturer
        self.config_path = config_path
        self.config = initial_config or self._load_config()
        self.client = client or Esp32LightClient(self.config.esp32_url)
        self.client.set_base_url(self.config.esp32_url)
        self.processor = BrightnessProcessor()

        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._active = False

        self._screen_brightness: float | None = None
        self._smoothed_luminance: float | None = None
        self._target_brightness: float | None = None
        self._last_sent_brightness: int | None = None
        self._esp32_reachable: bool | None = None
        self._last_error: str | None = None
        self._last_updated_at: float | None = None
        self._last_sent_at: float | None = None
        self._updates_sent: int = 0
        self._samples_taken: int = 0
        self._history: deque[dict[str, Any]] = deque(maxlen=60)

    def _load_config(self) -> SyncConfig:
        cfg = SyncConfig()
        if self.config_path and os.path.exists(self.config_path):
            try:
                with open(self.config_path, "r", encoding="utf-8") as f:
                    raw = json.load(f)
                cfg = SyncConfig(
                    esp32_url=str(raw.get("esp32Url", cfg.esp32_url)),
                    sensitivity=int(raw.get("sensitivity", cfg.sensitivity)),
                    min_brightness=int(raw.get("minBrightness", cfg.min_brightness)),
                    max_brightness=int(raw.get("maxBrightness", cfg.max_brightness)),
                    interval_ms=int(raw.get("intervalMs", cfg.interval_ms)),
                    dry_run=bool(raw.get("dryRun", cfg.dry_run)),
                )
                cfg.validate()
            except Exception:
                cfg = SyncConfig()
        return cfg

    def _save_config(self) -> None:
        if not self.config_path:
            return
        payload = {
            "esp32Url": self.config.esp32_url,
            "sensitivity": self.config.sensitivity,
            "minBrightness": self.config.min_brightness,
            "maxBrightness": self.config.max_brightness,
            "intervalMs": self.config.interval_ms,
            "dryRun": self.config.dry_run,
        }
        try:
            with open(self.config_path, "w", encoding="utf-8") as f:
                json.dump(payload, f, indent=2)
        except OSError:
            pass

    @property
    def is_active(self) -> bool:
        with self._lock:
            return self._active

    def start(self) -> bool:
        with self._lock:
            if self._active:
                return False
            self.processor.reset()
            self._stop_event.clear()
            self._active = True
            self._last_error = None
            self._thread = threading.Thread(target=self._loop, name="screen-sync", daemon=True)
            self._thread.start()
            return True

    def stop(self) -> bool:
        with self._lock:
            if not self._active:
                return False
            self._active = False
            self._stop_event.set()
            thread = self._thread
            self._thread = None
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=3.0)
        close_fn = getattr(self.capturer, "close", None)
        if callable(close_fn):
            close_fn()
        return True

    def update_config(self, patch: dict[str, Any]) -> SyncConfig:
        with self._lock:
            candidate = SyncConfig(
                esp32_url=str(patch.get("esp32Url", self.config.esp32_url)).strip().rstrip("/"),
                sensitivity=int(patch.get("sensitivity", self.config.sensitivity)),
                min_brightness=int(patch.get("minBrightness", self.config.min_brightness)),
                max_brightness=int(patch.get("maxBrightness", self.config.max_brightness)),
                interval_ms=int(patch.get("intervalMs", self.config.interval_ms)),
                dry_run=bool(patch.get("dryRun", self.config.dry_run)),
            )
            candidate.validate()
            self.config = candidate
            self.client.set_base_url(candidate.esp32_url)
            self._save_config()
            return candidate

    def run_once(self) -> dict[str, Any]:
        """Captures one frame, computes smoothed brightness, and pushes to ESP32 if needed."""
        with self._lock:
            cfg = SyncConfig(**asdict(self.config))

        try:
            cap = self.capturer.capture_luminance()
        except Exception as err:
            with self._lock:
                self._last_error = str(err)
                self._last_updated_at = time.time()
            return {"ok": False, "error": str(err)}

        params = SyncParameters(
            sensitivity=cfg.sensitivity,
            min_brightness=cfg.min_brightness,
            max_brightness=cfg.max_brightness,
        )

        with self._lock:
            step = self.processor.process(cap.luminance, params)
            self._screen_brightness = step.screen_luminance
            self._smoothed_luminance = step.smoothed_luminance
            self._target_brightness = step.target_brightness
            self._samples_taken += 1
            self._last_updated_at = time.time()

        sent_ok = True
        send_err: str | None = None
        if step.should_send:
            if cfg.dry_run:
                with self._lock:
                    self._last_sent_brightness = step.output_brightness
                    self._last_sent_at = time.time()
                    self._updates_sent += 1
                    self._last_error = None
            else:
                resp = self.client.set_brightness(step.output_brightness)
                with self._lock:
                    self._esp32_reachable = resp.ok
                    if resp.ok:
                        self._last_sent_brightness = step.output_brightness
                        self._last_sent_at = time.time()
                        self._updates_sent += 1
                        self._last_error = None
                    else:
                        sent_ok = False
                        send_err = f"ESP32 error: {resp.error}"
                        self._last_error = send_err
                        # Reset processor's last_sent so next tick retries if needed
                        self.processor._last_sent = self._last_sent_brightness
        else:
            with self._lock:
                if not cfg.dry_run and self._last_error and "Screen capture" in self._last_error:
                    self._last_error = None

        with self._lock:
            self._history.append({
                "t": int(time.time() * 1000),
                "screen": step.screen_luminance,
                "smoothed": step.smoothed_luminance,
                "light": self._last_sent_brightness if self._last_sent_brightness is not None else step.output_brightness,
            })

        return {
            "ok": sent_ok,
            "screenLuminance": step.screen_luminance,
            "outputBrightness": step.output_brightness,
            "sent": step.should_send,
            "error": send_err,
        }

    def check_esp32_health(self) -> dict[str, Any]:
        resp = self.client.get_status()
        with self._lock:
            self._esp32_reachable = resp.ok
            if not resp.ok:
                self._last_error = f"ESP32 unreachable: {resp.error}"
        return {"ok": resp.ok, "status": resp.data, "error": resp.error}

    def get_status(self) -> dict[str, Any]:
        with self._lock:
            return {
                "active": self._active,
                "config": {
                    "esp32Url": self.config.esp32_url,
                    "sensitivity": self.config.sensitivity,
                    "minBrightness": self.config.min_brightness,
                    "maxBrightness": self.config.max_brightness,
                    "intervalMs": self.config.interval_ms,
                    "dryRun": self.config.dry_run,
                },
                "metrics": {
                    "screenBrightness": self._screen_brightness,
                    "smoothedLuminance": self._smoothed_luminance,
                    "targetBrightness": self._target_brightness,
                    "lastSentBrightness": self._last_sent_brightness,
                    "esp32Reachable": self._esp32_reachable,
                    "updatesSent": self._updates_sent,
                    "samplesTaken": self._samples_taken,
                    "lastUpdatedAt": self._last_updated_at,
                    "lastSentAt": self._last_sent_at,
                    "lastError": self._last_error,
                },
                "history": list(self._history),
            }

    def _loop(self) -> None:
        while not self._stop_event.is_set():
            started = time.monotonic()
            self.run_once()
            with self._lock:
                interval_s = self.config.interval_ms / 1000.0
            elapsed = time.monotonic() - started
            wait_s = max(0.05, interval_s - elapsed)
            self._stop_event.wait(wait_s)

