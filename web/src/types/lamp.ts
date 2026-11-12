export type ProtocolId = 'v3' | 'v3_srvdata' | 'v2' | 'v1a' | 'v1b';

export interface EspStatusResponse {
  online: boolean;
  uptimeMs: number;
  wifi: {
    connected: boolean;
    apMode: boolean;
    ssid: string;
    ip: string;
    rssi: number;
    mdns: string;
  };
  ble: {
    initialized: boolean;
    error: string;
  };
  protocol: {
    id: ProtocolId;
    name: string;
    supportsRgb: boolean;
    supportsScenes: boolean;
  };
  device: {
    lightName: string;
    deviceType: number;
    deviceId: number;
    channel: number;
    deviceAddress: number;
    var1: number;
    paired: boolean;
    reversedCct: boolean;
    adFlag: number;
    advRepeats: number;
    advWindowMs: number;
    advEndMarker: boolean;
    mdnsHost: string;
  };
  lastTransmission: {
    success: boolean;
    status: string;
    message: string;
    command: string;
    protocol: string;
    txCount: number;
    timestampMs: number;
    frameHex: string;
  };
}

export interface LampStateResponse {
  pushOnly: boolean;
  note: string;
  power: boolean;
  brightness: number;
  temperature: number;
  colorMode: 'cct' | 'rgb' | 'scene' | 'night';
  supportsRgb: boolean;
  color: {
    h: number;
    s: number;
    v: number;
    r: number;
    g: number;
    b: number;
  };
  scene: number;
  lastCommandTimestampMs: number;
}

export interface ApiCommandResponse {
  success: boolean;
  status: string;
  message: string;
  txCount?: number;
  frameHex?: string;
}

export interface DeviceConfigPayload {
  lightName?: string;
  protocol?: ProtocolId;
  deviceType?: number;
  deviceId?: number;
  channel?: number;
  deviceAddress?: number;
  paired?: boolean;
  reversedCct?: boolean;
  adFlag?: number;
  advRepeats?: number;
  advWindowMs?: number;
  advEndMarker?: boolean;
  mdnsHost?: string;
}

