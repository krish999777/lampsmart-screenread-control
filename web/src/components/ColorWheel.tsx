import React, { useEffect, useRef } from 'react';
import { hsvToRgb } from '../utils/color';

interface ColorWheelProps {
  hue: number;        // 0..360
  saturation: number; // 0..100
  disabled?: boolean;
  size?: number;
  onChange: (h: number, s: number) => void;
  onChangeEnd: (h: number, s: number) => void;
}

export const ColorWheel: React.FC<ColorWheelProps> = ({
  hue,
  saturation,
  disabled = false,
  size = 220,
  onChange,
  onChangeEnd,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const draggingRef = useRef(false);
  const latestRef = useRef({ h: hue, s: saturation });

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = size * dpr;
    canvas.height = size * dpr;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const img = ctx.createImageData(canvas.width, canvas.height);
    const data = img.data;
    const cx = canvas.width / 2;
    const cy = canvas.height / 2;
    const radius = Math.min(cx, cy) - 2 * dpr;

    for (let y = 0; y < canvas.height; y++) {
      for (let x = 0; x < canvas.width; x++) {
        const dx = x - cx;
        const dy = y - cy;
        const dist = Math.sqrt(dx * dx + dy * dy);
        const idx = (y * canvas.width + x) * 4;

        if (dist <= radius) {
          let angle = (Math.atan2(dy, dx) * 180) / Math.PI;
          if (angle < 0) angle += 360;
          const sat = (dist / radius) * 100;
          const { r, g, b } = hsvToRgb(angle, sat, 100);
          const edgeAlpha = dist > radius - dpr ? Math.max(0, (radius - dist) / dpr) : 1;
          data[idx] = r;
          data[idx + 1] = g;
          data[idx + 2] = b;
          data[idx + 3] = Math.round(edgeAlpha * 255);
        } else {
          data[idx + 3] = 0;
        }
      }
    }
    ctx.putImageData(img, 0, 0);
  }, [size]);

  const pickAt = (clientX: number, clientY: number) => {
    const canvas = canvasRef.current;
    if (!canvas) return { h: hue, s: saturation };
    const rect = canvas.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    const radius = rect.width / 2 - 2;

    const dx = clientX - cx;
    const dy = clientY - cy;
    let angle = (Math.atan2(dy, dx) * 180) / Math.PI;
    if (angle < 0) angle += 360;
    const dist = Math.min(radius, Math.sqrt(dx * dx + dy * dy));
    const sat = Math.round((dist / radius) * 100);
    const h = Math.round(angle) % 360;
    latestRef.current = { h, s: sat };
    return { h, s: sat };
  };

  const handlePointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (disabled) return;
    draggingRef.current = true;
    e.currentTarget.setPointerCapture(e.pointerId);
    const next = pickAt(e.clientX, e.clientY);
    onChange(next.h, next.s);
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    if (disabled || !draggingRef.current) return;
    const next = pickAt(e.clientX, e.clientY);
    onChange(next.h, next.s);
  };

  const handlePointerUp = () => {
    if (disabled || !draggingRef.current) return;
    draggingRef.current = false;
    onChangeEnd(latestRef.current.h, latestRef.current.s);
  };

  const rad = (hue * Math.PI) / 180;
  const rFrac = Math.max(0, Math.min(100, saturation)) / 100;
  const maxR = size / 2 - 4;
  const knobX = size / 2 + Math.cos(rad) * rFrac * maxR;
  const knobY = size / 2 + Math.sin(rad) * rFrac * maxR;
  const knobRgb = hsvToRgb(hue, saturation, 100);

  return (
    <div
      className={`color-wheel-wrap ${disabled ? 'is-disabled' : ''}`}
      style={{ width: size, height: size }}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
      role="slider"
      aria-label="Color wheel hue and saturation"
      aria-valuetext={`Hue ${hue}°, Saturation ${saturation}%`}
    >
      <canvas ref={canvasRef} style={{ width: size, height: size }} />
      <div
        className="color-wheel-knob"
        style={{
          left: `${knobX}px`,
          top: `${knobY}px`,
          backgroundColor: `rgb(${knobRgb.r}, ${knobRgb.g}, ${knobRgb.b})`,
        }}
      />
    </div>
  );
};

