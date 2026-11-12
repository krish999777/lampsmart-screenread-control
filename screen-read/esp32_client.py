"""Fast persistent HTTP client with cached mDNS/DNS resolution for the ESP32 API."""

from __future__ import annotations

import http.client
import json
import socket
import threading
import time
from dataclasses import dataclass
from typing import Any
from urllib.parse import urlparse


@dataclass(frozen=True)
class Esp32Response:
    ok: bool
    status_code: int
    data: dict[str, Any]
    error: str = ""


class Esp32LightClient:
    """Persistent keep-alive HTTP client over the existing ESP32 REST API."""

    def __init__(self, base_url: str = "http://lampsmart.local", timeout_s: float = 2.5) -> None:
        self._lock = threading.Lock()
        self.base_url = base_url.rstrip("/")
        self.timeout_s = timeout_s
        self._conn: http.client.HTTPConnection | None = None
        self._resolved_host: str | None = None
        self._resolved_ip: str | None = None
        self._resolved_at: float = 0.0

    def set_base_url(self, base_url: str) -> None:
        normalized = base_url.strip().rstrip("/")
        with self._lock:
            if normalized != self.base_url:
                self.base_url = normalized
                self._close_conn_locked()
                self._resolved_host = None
                self._resolved_ip = None
                self._resolved_at = 0.0

    def get_status(self) -> Esp32Response:
        return self._request("GET", "/api/v1/status")

    def set_brightness(self, brightness: int) -> Esp32Response:
        clamped = max(1, min(100, int(brightness)))
        return self._request("POST", "/api/v1/light/brightness", {"brightness": clamped})

    def _close_conn_locked(self) -> None:
        if self._conn is not None:
            try:
                self._conn.close()
            except Exception:
                pass
            self._conn = None

    def _resolve_ip_locked(self, hostname: str, port: int) -> str:
        now = time.monotonic()
        if self._resolved_host == hostname and self._resolved_ip and (now - self._resolved_at) < 60.0:
            return self._resolved_ip
        infos = socket.getaddrinfo(hostname, port, socket.AF_INET, socket.SOCK_STREAM)
        ip = infos[0][4][0]
        self._resolved_host = hostname
        self._resolved_ip = ip
        self._resolved_at = now
        return ip

    def _request(self, method: str, path: str, payload: dict[str, Any] | None = None) -> Esp32Response:
        with self._lock:
            parsed = urlparse(self.base_url)
            scheme = parsed.scheme or "http"
            hostname = parsed.hostname or "lampsmart.local"
            port = parsed.port or (443 if scheme == "https" else 80)
            base_path = parsed.path.rstrip("/")
            full_path = f"{base_path}{path}"

            body = json.dumps(payload).encode("utf-8") if payload is not None else None
            headers = {
                "Host": parsed.netloc or hostname,
                "Connection": "keep-alive",
            }
            if body is not None:
                headers["Content-Type"] = "application/json"
                headers["Content-Length"] = str(len(body))

            last_err: Exception | None = None
            for attempt in range(2):
                try:
                    target_ip = self._resolve_ip_locked(hostname, port)
                    if self._conn is None:
                        cls = http.client.HTTPSConnection if scheme == "https" else http.client.HTTPConnection
                        self._conn = cls(target_ip, port, timeout=self.timeout_s)

                    self._conn.request(method, full_path, body=body, headers=headers)
                    resp = self._conn.getresponse()
                    raw = resp.read().decode("utf-8", errors="replace")
                    data = json.loads(raw) if raw else {}
                    if 200 <= resp.status < 300:
                        return Esp32Response(ok=True, status_code=resp.status, data=data)
                    msg = data.get("message") or f"HTTP {resp.status}"
                    return Esp32Response(ok=False, status_code=resp.status, data=data, error=msg)
                except Exception as err:
                    last_err = err
                    self._close_conn_locked()
                    if attempt == 0:
                        self._resolved_at = 0.0  # force re-resolve on retry

            return Esp32Response(ok=False, status_code=0, data={}, error=str(last_err))
