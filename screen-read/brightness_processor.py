"""Direct, low-latency brightness mapping with configurable noise deadband."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass
class SyncParameters:
    sensitivity: int = 75      # 1..100 (higher = smaller deadband / more responsive)
    min_brightness: int = 10   # 1..100
    max_brightness: int = 95   # 1..100

    def validate(self) -> None:
        if not (1 <= self.sensitivity <= 100):
            raise ValueError("sensitivity must be between 1 and 100")
        if not (1 <= self.min_brightness <= 100):
            raise ValueError("min_brightness must be between 1 and 100")
        if not (1 <= self.max_brightness <= 100):
            raise ValueError("max_brightness must be between 1 and 100")
        if self.min_brightness > self.max_brightness:
            raise ValueError("min_brightness cannot exceed max_brightness")


@dataclass
class BrightnessStep:
    screen_luminance: float
    smoothed_luminance: float
    target_brightness: float
    output_brightness: int
    should_send: bool


class BrightnessProcessor:
    """
    Maps 0..100% visual screen luminance directly to [min_brightness, max_brightness]
    with zero gradual ramping delay, using only a sensitivity-based deadband
    to avoid resending identical or noisy values.
    """

    def __init__(self) -> None:
        self._last_sent: int | None = None

    def reset(self) -> None:
        self._last_sent = None

    @staticmethod
    def _deadband_for_sensitivity(sensitivity: int) -> int:
        """
        Derives integer deadband (0..5%) from sensitivity (1..100).
        - sensitivity 90..100 -> deadband 1 (sends every >=1% change, or 0 at 100)
        - sensitivity 75      -> deadband 1
        - sensitivity 50      -> deadband 2
        - sensitivity 1       -> deadband 5
        """
        s = max(1, min(100, sensitivity))
        if s == 100:
            return 1
        return max(1, round((100 - s) * 0.05))

    def process(self, screen_luminance: float, params: SyncParameters) -> BrightnessStep:
        params.validate()
        lum = max(0.0, min(100.0, float(screen_luminance)))

        # Direct linear mapping: darker screen -> min_brightness, brighter screen -> max_brightness
        span = params.max_brightness - params.min_brightness
        target = params.min_brightness + (lum / 100.0) * span
        output = int(round(max(params.min_brightness, min(params.max_brightness, target))))
        output = max(1, min(100, output))

        deadband = self._deadband_for_sensitivity(params.sensitivity)
        should_send = self._last_sent is None or abs(output - self._last_sent) >= deadband

        if should_send:
            self._last_sent = output

        return BrightnessStep(
            screen_luminance=round(lum, 1),
            smoothed_luminance=round(lum, 1),
            target_brightness=round(target, 1),
            output_brightness=output,
            should_send=should_send,
        )
