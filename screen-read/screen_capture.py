"""Screen capture and perceptual visual luminance extraction."""

from __future__ import annotations

import os
import struct
import subprocess
import tempfile
import threading
from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True)
class CaptureResult:
    luminance: float  # 0.0 (pure black) .. 100.0 (pure white)
    width: int
    height: int


class ScreenCapturer(Protocol):
    def capture_luminance(self) -> CaptureResult:
        ...


def parse_bmp_luminance(bmp_bytes: bytes) -> CaptureResult:
    """Computes average perceptual luminance (ITU-R BT.709, 0..100) from an uncompressed 24/32-bit BMP."""
    if len(bmp_bytes) < 54 or bmp_bytes[0:2] != b"BM":
        raise ValueError("Invalid BMP header")

    pixel_offset = struct.unpack_from("<I", bmp_bytes, 10)[0]
    width, raw_height = struct.unpack_from("<ii", bmp_bytes, 18)
    bpp = struct.unpack_from("<H", bmp_bytes, 28)[0]
    compression = struct.unpack_from("<I", bmp_bytes, 30)[0]

    height = abs(raw_height)
    if width <= 0 or height <= 0:
        raise ValueError(f"Invalid BMP dimensions: {width}x{raw_height}")
    if bpp not in (24, 32):
        raise ValueError(f"Unsupported BMP bit depth: {bpp}")
    if compression not in (0, 3):  # BI_RGB or BI_BITFIELDS
        raise ValueError(f"Unsupported BMP compression: {compression}")

    bytes_per_px = bpp // 8
    row_stride = (width * bytes_per_px + 3) & ~3
    required_len = pixel_offset + row_stride * height
    if len(bmp_bytes) < required_len:
        raise ValueError("Truncated BMP pixel payload")

    total_lum = 0.0
    total_pixels = width * height

    for y in range(height):
        row_start = pixel_offset + y * row_stride
        for x in range(width):
            px = row_start + x * bytes_per_px
            b = bmp_bytes[px]
            g = bmp_bytes[px + 1]
            r = bmp_bytes[px + 2]
            total_lum += (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0

    avg_pct = (total_lum / total_pixels) * 100.0
    return CaptureResult(luminance=round(avg_pct, 2), width=width, height=height)


class MacOsScreenCapturer:
    """
    Fast macOS screen luminance capturer.
    Prefers the persistent native ScreenCaptureKit helper (`mac_screen_lum`, ~10ms, zero disk I/O),
    and falls back to `screencapture` + `sips` if unavailable.
    """

    def __init__(self, display_index: int = 1, sample_size: int = 64) -> None:
        self.display_index = display_index
        self.sample_size = sample_size
        self._dir = os.path.dirname(os.path.abspath(__file__))
        self._swift_src = os.path.join(self._dir, "mac_screen_lum.swift")
        self._helper_bin = os.path.join(self._dir, "mac_screen_lum")
        self._proc: subprocess.Popen[str] | None = None
        self._proc_lock = threading.Lock()

    def _ensure_helper_binary(self) -> bool:
        if os.path.exists(self._helper_bin):
            if not os.path.exists(self._swift_src) or (
                os.path.getmtime(self._helper_bin) >= os.path.getmtime(self._swift_src)
            ):
                return True
        if not os.path.exists(self._swift_src):
            return False
        try:
            proc = subprocess.run(
                ["/usr/bin/swiftc", "-O", "-parse-as-library", self._swift_src, "-o", self._helper_bin],
                capture_output=True,
                text=True,
                timeout=30,
            )
            return proc.returncode == 0 and os.path.exists(self._helper_bin)
        except Exception:
            return False

    def _capture_via_helper(self) -> CaptureResult:
        with self._proc_lock:
            if self._proc is None or self._proc.poll() is not None:
                if not self._ensure_helper_binary():
                    raise RuntimeError("Native helper binary unavailable")
                self._proc = subprocess.Popen(
                    [self._helper_bin, str(self.display_index)],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    bufsize=1,
                )

            assert self._proc.stdin is not None
            assert self._proc.stdout is not None
            self._proc.stdin.write("cap\n")
            self._proc.stdin.flush()
            line = self._proc.stdout.readline().strip()

        if line.startswith("OK:"):
            parts = line[3:].split(",")
            return CaptureResult(
                luminance=float(parts[0]),
                width=int(parts[1]),
                height=int(parts[2]),
            )

        self.close()
        err_detail = line[4:] if line.startswith("ERR:") else "empty response"
        raise RuntimeError(f"ScreenCaptureKit capture failed: {err_detail}")

    def _capture_via_screencapture(self) -> CaptureResult:
        fd_raw, raw_path = tempfile.mkstemp(prefix="sr_raw_", suffix=".bmp")
        fd_small, small_path = tempfile.mkstemp(prefix="sr_sm_", suffix=".bmp")
        os.close(fd_raw)
        os.close(fd_small)

        try:
            cap_cmd = [
                "/usr/sbin/screencapture",
                "-x",
                "-t", "bmp",
                f"-D{self.display_index}",
                raw_path,
            ]
            proc = subprocess.run(cap_cmd, capture_output=True, text=True, timeout=5)
            if proc.returncode != 0 or not os.path.exists(raw_path) or os.path.getsize(raw_path) == 0:
                stderr = (proc.stderr or "").strip()
                raise RuntimeError(
                    f"Screen capture failed (rc={proc.returncode}). "
                    f"Ensure Screen Recording permission is granted to your terminal. {stderr}"
                )

            sips_cmd = [
                "/usr/bin/sips",
                "-z", str(self.sample_size), str(self.sample_size),
                "-s", "format", "bmp",
                raw_path,
                "--out", small_path,
            ]
            sips_proc = subprocess.run(sips_cmd, capture_output=True, text=True, timeout=5)
            if sips_proc.returncode != 0:
                raise RuntimeError(f"sips downscale failed: {sips_proc.stderr.strip()}")

            with open(small_path, "rb") as f:
                bmp_data = f.read()

            return parse_bmp_luminance(bmp_data)
        finally:
            for p in (raw_path, small_path):
                try:
                    if os.path.exists(p):
                        os.remove(p)
                except OSError:
                    pass

    def capture_luminance(self) -> CaptureResult:
        try:
            return self._capture_via_helper()
        except Exception:
            return self._capture_via_screencapture()

    def close(self) -> None:
        with self._proc_lock:
            if self._proc is not None:
                try:
                    if self._proc.stdin:
                        self._proc.stdin.write("quit\n")
                        self._proc.stdin.flush()
                    self._proc.terminate()
                    self._proc.wait(timeout=1.0)
                except Exception:
                    try:
                        self._proc.kill()
                    except Exception:
                        pass
                finally:
                    self._proc = None


class SimulatedScreenCapturer:
    """Deterministic screen luminance source for testing or dry-run demos."""

    def __init__(self, sequence: list[float] | None = None) -> None:
        self.sequence = sequence or [15.0, 20.0, 75.0, 80.0, 30.0]
        self._idx = 0

    def capture_luminance(self) -> CaptureResult:
        val = self.sequence[self._idx % len(self.sequence)]
        self._idx += 1
        return CaptureResult(luminance=val, width=64, height=64)
