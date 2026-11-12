import type {
  ApiCommandResponse,
  DeviceConfigPayload,
  EspStatusResponse,
  LampStateResponse,
} from '../types/lamp';

const BASE_URL_STORAGE_KEY = 'lampsmart.esp32BaseUrl';

export function getSavedBaseUrl(): string {
  try {
    return (localStorage.getItem(BASE_URL_STORAGE_KEY) || '').trim().replace(/\/+$/, '');
  } catch {
    return '';
  }
}

export function saveBaseUrl(url: string): string {
  const normalized = url.trim().replace(/\/+$/, '');
  try {
    if (normalized) {
      localStorage.setItem(BASE_URL_STORAGE_KEY, normalized);
    } else {
      localStorage.removeItem(BASE_URL_STORAGE_KEY);
    }
  } catch {
    // ignore storage errors
  }
  return normalized;
}

export class EspApiError extends Error {
  readonly statusCode: number;
  readonly apiStatus: string;

  constructor(message: string, statusCode = 0, apiStatus = 'network_error') {
    super(message);
    this.name = 'EspApiError';
    this.statusCode = statusCode;
    this.apiStatus = apiStatus;
  }
}

async function requestJson<T>(
  path: string,
  options: RequestInit = {},
  baseUrl = getSavedBaseUrl(),
  timeoutMs = 5000
): Promise<T> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  const url = `${baseUrl}${path}`;

  try {
    const res = await fetch(url, {
      ...options,
      headers: {
        ...(options.body ? { 'Content-Type': 'application/json' } : {}),
        ...(options.headers || {}),
      },
      signal: controller.signal,
    });

    const text = await res.text();
    let parsed: Record<string, unknown> = {};
    if (text) {
      try {
        parsed = JSON.parse(text) as Record<string, unknown>;
      } catch {
        throw new EspApiError(`Invalid JSON response (${res.status})`, res.status, 'invalid_json');
      }
    }

    if (!res.ok) {
      const msg = typeof parsed.message === 'string' ? parsed.message : `HTTP ${res.status}`;
      const st = typeof parsed.status === 'string' ? parsed.status : 'http_error';
      throw new EspApiError(msg, res.status, st);
    }

    return parsed as unknown as T;
  } catch (err) {
    if (err instanceof EspApiError) throw err;
    if (err instanceof DOMException && err.name === 'AbortError') {
      throw new EspApiError('Request to ESP32 timed out', 0, 'timeout');
    }
    throw new EspApiError(
      err instanceof Error ? err.message : 'Unable to reach ESP32',
      0,
      'network_error'
    );
  } finally {
    clearTimeout(timer);
  }
}

export const lampApi = {
  getStatus: (baseUrl?: string) =>
    requestJson<EspStatusResponse>('/api/v1/status', { method: 'GET' }, baseUrl),

  getState: (baseUrl?: string) =>
    requestJson<LampStateResponse>('/api/v1/state', { method: 'GET' }, baseUrl),

  pair: (baseUrl?: string) =>
    requestJson<ApiCommandResponse>('/api/v1/pair', { method: 'POST', body: '{}' }, baseUrl),

  unpair: (baseUrl?: string) =>
    requestJson<ApiCommandResponse>('/api/v1/unpair', { method: 'POST', body: '{}' }, baseUrl),

  setPower: (on: boolean, baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/light/power',
      { method: 'POST', body: JSON.stringify({ on }) },
      baseUrl
    ),

  setBrightness: (brightness: number, baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/light/brightness',
      { method: 'POST', body: JSON.stringify({ brightness: Math.round(brightness) }) },
      baseUrl
    ),

  setTemperature: (temperature: number, baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/light/temperature',
      { method: 'POST', body: JSON.stringify({ temperature: Math.round(temperature) }) },
      baseUrl
    ),

  setColorHsv: (h: number, s: number, v: number, baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/light/color',
      {
        method: 'POST',
        body: JSON.stringify({
          h: Math.round(h),
          s: Math.round(s),
          v: Math.round(v),
        }),
      },
      baseUrl
    ),

  setColorRgb: (r: number, g: number, b: number, brightness?: number, baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/light/color',
      {
        method: 'POST',
        body: JSON.stringify({
          r: Math.round(r),
          g: Math.round(g),
          b: Math.round(b),
          ...(brightness !== undefined ? { brightness: Math.round(brightness) } : {}),
        }),
      },
      baseUrl
    ),

  setScene: (scene: number, baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/light/scene',
      { method: 'POST', body: JSON.stringify({ scene }) },
      baseUrl
    ),

  sceneAction: (action: 'on' | 'reset', baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/light/scene',
      { method: 'POST', body: JSON.stringify({ action }) },
      baseUrl
    ),

  nightLight: (baseUrl?: string) =>
    requestJson<ApiCommandResponse>('/api/v1/light/night', { method: 'POST', body: '{}' }, baseUrl),

  updateDeviceConfig: (config: DeviceConfigPayload, baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/device/config',
      { method: 'POST', body: JSON.stringify(config) },
      baseUrl
    ),

  testCommand: (
    raw?: { rawCommand: number; p21: number; p22: number; p23: number },
    baseUrl?: string
  ) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/device/test',
      { method: 'POST', body: JSON.stringify(raw ?? {}) },
      baseUrl
    ),

  configureWifi: (ssid: string, password: string, baseUrl?: string) =>
    requestJson<ApiCommandResponse>(
      '/api/v1/wifi/config',
      { method: 'POST', body: JSON.stringify({ ssid, password }) },
      baseUrl
    ),
};

