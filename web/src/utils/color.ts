export interface RgbColor {
  r: number;
  g: number;
  b: number;
}

export interface HsvColor {
  h: number; // 0..360
  s: number; // 0..100
  v: number; // 0..100
}

export function hsvToRgb(h: number, s: number, v: number): RgbColor {
  const hue = ((h % 360) + 360) % 360;
  const sat = Math.max(0, Math.min(100, s)) / 100;
  const val = Math.max(0, Math.min(100, v)) / 100;

  const c = val * sat;
  const x = c * (1 - Math.abs(((hue / 60) % 2) - 1));
  const m = val - c;

  let rf = 0;
  let gf = 0;
  let bf = 0;
  if (hue < 60) {
    rf = c; gf = x; bf = 0;
  } else if (hue < 120) {
    rf = x; gf = c; bf = 0;
  } else if (hue < 180) {
    rf = 0; gf = c; bf = x;
  } else if (hue < 240) {
    rf = 0; gf = x; bf = c;
  } else if (hue < 300) {
    rf = x; gf = 0; bf = c;
  } else {
    rf = c; gf = 0; bf = x;
  }

  return {
    r: Math.round((rf + m) * 255),
    g: Math.round((gf + m) * 255),
    b: Math.round((bf + m) * 255),
  };
}

export function rgbToHsv(r: number, g: number, b: number): HsvColor {
  const rf = Math.max(0, Math.min(255, r)) / 255;
  const gf = Math.max(0, Math.min(255, g)) / 255;
  const bf = Math.max(0, Math.min(255, b)) / 255;

  const max = Math.max(rf, gf, bf);
  const min = Math.min(rf, gf, bf);
  const d = max - min;

  let h = 0;
  if (d !== 0) {
    if (max === rf) h = 60 * (((gf - bf) / d) % 6);
    else if (max === gf) h = 60 * ((bf - rf) / d + 2);
    else h = 60 * ((rf - gf) / d + 4);
  }
  if (h < 0) h += 360;

  const s = max === 0 ? 0 : (d / max) * 100;
  const v = max * 100;

  return {
    h: Math.round(h),
    s: Math.round(s),
    v: Math.round(v),
  };
}

export function rgbToHex({ r, g, b }: RgbColor): string {
  const toHex = (n: number) => Math.max(0, Math.min(255, Math.round(n))).toString(16).padStart(2, '0');
  return `#${toHex(r)}${toHex(g)}${toHex(b)}`.toUpperCase();
}

export function hexToRgb(hex: string): RgbColor {
  const clean = hex.replace('#', '');
  return {
    r: parseInt(clean.slice(0, 2), 16),
    g: parseInt(clean.slice(2, 4), 16),
    b: parseInt(clean.slice(4, 6), 16),
  };
}

/// 12 color presets matching xLamp's palette
export const XLAMP_COLOR_PRESETS: ReadonlyArray<{ hex: string; name: string }> = [
  { hex: '#FFD9A0', name: 'Warm Glow' },
  { hex: '#FFB4A2', name: 'Peach' },
  { hex: '#FF8A80', name: 'Coral' },
  { hex: '#F06292', name: 'Rose' },
  { hex: '#BA68C8', name: 'Orchid' },
  { hex: '#7986CB', name: 'Indigo' },
  { hex: '#64B5F6', name: 'Sky' },
  { hex: '#4DD0E1', name: 'Cyan' },
  { hex: '#4DB6AC', name: 'Teal' },
  { hex: '#81C784', name: 'Mint' },
  { hex: '#DCE775', name: 'Lime' },
  { hex: '#FFFFFF', name: 'Pure White' },
];

export function cctPreviewColor(temperature: number): string {
  // 0 = warm (#FFB469), 50 = neutral (#FFF4E0), 100 = cool (#CFE8FF)
  const t = Math.max(0, Math.min(100, temperature)) / 100;
  if (t <= 0.5) {
    const f = t * 2;
    const r = Math.round(255);
    const g = Math.round(180 + (244 - 180) * f);
    const b = Math.round(105 + (224 - 105) * f);
    return `rgb(${r}, ${g}, ${b})`;
  }
  const f = (t - 0.5) * 2;
  const r = Math.round(255 + (207 - 255) * f);
  const g = Math.round(244 + (232 - 244) * f);
  const b = Math.round(224 + (255 - 224) * f);
  return `rgb(${r}, ${g}, ${b})`;
}

