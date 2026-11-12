import React, { useEffect, useState } from 'react';
import { getSavedBaseUrl, lampApi, saveBaseUrl } from '../services/api';
import type { EspStatusResponse, ProtocolId } from '../types/lamp';

interface SettingsViewProps {
  status: EspStatusResponse | null;
  connected: boolean;
  onRefresh: () => Promise<void>;
  onOpenPairing: () => void;
  onNotify: (msg: string, isError?: boolean) => void;
}

const PROTOCOL_OPTIONS: Array<{ id: ProtocolId; label: string; note: string }> = [
  { id: 'v3', label: 'LampSmart Pro V3 (Default)', note: '13× Service UUIDs + AES-128 (RGB + CCT + Scenes)' },
  { id: 'v3_srvdata', label: 'LampSmart Pro V3 (Service Data)', note: 'AD Type 0x16 + AES-128 (RGB + CCT + Scenes)' },
  { id: 'v2', label: 'LampSmart Pro V2 (Unsigned)', note: '13× Service UUIDs, no AES signature' },
  { id: 'v1a', label: 'LampSmart Legacy V1a', note: 'Older 0x77 0xF8 fixtures (CCT Only — No RGB)' },
  { id: 'v1b', label: 'LampSmart Legacy V1b', note: 'Older 0xF9 0x08 fixtures (CCT Only — No RGB)' },
];

const DEVICE_TYPES = [
  { value: 256, label: 'Light / Ceiling Lamp (256)' },
  { value: 512, label: 'Fan Light (512)' },
  { value: 1024, label: 'Exhaust / FLP (1024)' },
  { value: 2048, label: 'Bath Heater (2048)' },
];

export const SettingsView: React.FC<SettingsViewProps> = ({
  status,
  connected,
  onRefresh,
  onOpenPairing,
  onNotify,
}) => {
  const [espUrl, setEspUrl] = useState(getSavedBaseUrl());
  const [lightName, setLightName] = useState('LampSmart Light');
  const [protocol, setProtocol] = useState<ProtocolId>('v3');
  const [deviceType, setDeviceType] = useState(256);
  const [deviceId, setDeviceId] = useState(0);
  const [channel, setChannel] = useState(0);
  const [deviceAddress, setDeviceAddress] = useState(1);
  const [reversedCct, setReversedCct] = useState(false);
  const [advRepeats, setAdvRepeats] = useState(8);
  const [advWindowMs, setAdvWindowMs] = useState(500);
  const [advEndMarker, setAdvEndMarker] = useState(true);
  const [wifiSsid, setWifiSsid] = useState('');
  const [wifiPass, setWifiPass] = useState('');
  const [saving, setSaving] = useState(false);
  const [testing, setTesting] = useState(false);

  useEffect(() => {
    if (!status) return;
    setLightName(status.device.lightName);
    setProtocol(status.protocol.id);
    setDeviceType(status.device.deviceType);
    setDeviceId(status.device.deviceId);
    setChannel(status.device.channel);
    setDeviceAddress(status.device.deviceAddress);
    setReversedCct(status.device.reversedCct);
    setAdvRepeats(status.device.advRepeats);
    setAdvWindowMs(status.device.advWindowMs);
    setAdvEndMarker(status.device.advEndMarker);
  }, [status]);

  const handleSaveHost = async (e: React.FormEvent) => {
    e.preventDefault();
    saveBaseUrl(espUrl);
    await onRefresh();
    onNotify('ESP32 endpoint updated');
  };

  const handleSaveDevice = async (e: React.FormEvent) => {
    e.preventDefault();
    setSaving(true);
    try {
      await lampApi.updateDeviceConfig({
        lightName: lightName.trim() || 'LampSmart Light',
        protocol,
        deviceType,
        deviceId: Number(deviceId),
        channel: Number(channel),
        deviceAddress: Number(deviceAddress),
        reversedCct,
        advRepeats: Number(advRepeats),
        advWindowMs: Number(advWindowMs),
        advEndMarker,
      });
      await onRefresh();
      onNotify('Device configuration saved to ESP32 NVS');
    } catch (err) {
      onNotify(err instanceof Error ? err.message : 'Failed to save configuration', true);
    } finally {
      setSaving(false);
    }
  };

  const handleTestCommand = async () => {
    setTesting(true);
    try {
      const res = await lampApi.testCommand();
      await onRefresh();
      onNotify(`Test packet transmitted (${res.frameHex?.slice(0, 16) ?? 'ok'}...)`);
    } catch (err) {
      onNotify(err instanceof Error ? err.message : 'Test command failed', true);
    } finally {
      setTesting(false);
    }
  };

  const handleSaveWifi = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!wifiSsid.trim()) return;
    setSaving(true);
    try {
      const res = await lampApi.configureWifi(wifiSsid.trim(), wifiPass);
      onNotify(res.message);
      setWifiPass('');
    } catch (err) {
      onNotify(err instanceof Error ? err.message : 'Failed to update Wi-Fi', true);
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="settings-stack">
      <section className="card">
        <div className="card-header">
          <div>
            <h3>ESP32 Connection</h3>
            <p className="muted">Leave host empty when opening directly from the ESP32.</p>
          </div>
          <span className={`pill ${connected ? 'pill-ok' : 'pill-err'}`}>
            {connected ? 'Connected' : 'Offline'}
          </span>
        </div>

        <form onSubmit={handleSaveHost} className="form-row-inline">
          <input
            type="text"
            value={espUrl}
            onChange={(e) => setEspUrl(e.target.value)}
            placeholder="Same-origin (default) or http://lampsmart.local"
            aria-label="ESP32 Host URL"
          />
          <button className="btn btn-secondary" type="submit">
            Connect
          </button>
          <button className="btn btn-ghost" type="button" onClick={onRefresh}>
            Refresh
          </button>
        </form>

        {status && (
          <div className="diag-grid">
            <div>
              <span className="diag-label">Wi-Fi</span>
              <strong>{status.wifi.ssid || '—'} ({status.wifi.ip})</strong>
            </div>
            <div>
              <span className="diag-label">mDNS</span>
              <strong>{status.wifi.mdns}</strong>
            </div>
            <div>
              <span className="diag-label">BLE Stack</span>
              <strong>{status.ble.initialized ? 'Initialized' : status.ble.error || 'Not Ready'}</strong>
            </div>
            <div>
              <span className="diag-label">Last TX</span>
              <strong>
                {status.lastTransmission.command} ({status.lastTransmission.status})
              </strong>
            </div>
            {status.lastTransmission.frameHex && (
              <div className="diag-full">
                <span className="diag-label">Last BLE Frame (31B)</span>
                <code className="hex-code">{status.lastTransmission.frameHex}</code>
              </div>
            )}
          </div>
        )}

        <div className="card-actions">
          <button
            className="btn btn-secondary"
            type="button"
            onClick={handleTestCommand}
            disabled={!connected || testing}
          >
            {testing ? 'Sending Test...' : 'Send Test Command (Power On)'}
          </button>
          <button className="btn btn-primary" type="button" onClick={onOpenPairing} disabled={!connected}>
            {status?.device.paired ? 'Pairing & Unpair' : 'Pair Light Now'}
          </button>
        </div>
      </section>

      <section className="card">
        <div className="card-header">
          <div>
            <h3>Device &amp; Protocol</h3>
            <p className="muted">
              Pairing status: <strong>{status?.device.paired ? 'Paired' : 'Not paired'}</strong>
            </p>
          </div>
        </div>

        <form onSubmit={handleSaveDevice} className="form-grid">
          <label>
            <span>Light Name</span>
            <input
              type="text"
              value={lightName}
              onChange={(e) => setLightName(e.target.value)}
              maxLength={40}
            />
          </label>

          <label>
            <span>Protocol Version</span>
            <select value={protocol} onChange={(e) => setProtocol(e.target.value as ProtocolId)}>
              {PROTOCOL_OPTIONS.map((p) => (
                <option key={p.id} value={p.id}>
                  {p.label}
                </option>
              ))}
            </select>
            <small className="muted">
              {PROTOCOL_OPTIONS.find((p) => p.id === protocol)?.note}
            </small>
          </label>

          <label>
            <span>Device Type</span>
            <select
              value={deviceType}
              onChange={(e) => setDeviceType(Number(e.target.value))}
            >
              {DEVICE_TYPES.map((dt) => (
                <option key={dt.value} value={dt.value}>
                  {dt.label}
                </option>
              ))}
            </select>
          </label>

          <div className="form-split-3">
            <label>
              <span>Device ID (var1 base)</span>
              <input
                type="number"
                min={1}
                max={2147483647}
                value={deviceId}
                onChange={(e) => setDeviceId(Number(e.target.value))}
              />
            </label>
            <label>
              <span>Channel (0–15)</span>
              <input
                type="number"
                min={0}
                max={15}
                value={channel}
                onChange={(e) => setChannel(Number(e.target.value))}
              />
            </label>
            <label>
              <span>Address (var2)</span>
              <input
                type="number"
                min={0}
                max={255}
                value={deviceAddress}
                onChange={(e) => setDeviceAddress(Number(e.target.value))}
              />
            </label>
          </div>

          <div className="form-split-2">
            <label>
              <span>TX Windows (repeats)</span>
              <input
                type="number"
                min={1}
                max={32}
                value={advRepeats}
                onChange={(e) => setAdvRepeats(Number(e.target.value))}
              />
            </label>
            <label>
              <span>Window Duration (ms)</span>
              <input
                type="number"
                min={50}
                max={2000}
                step={50}
                value={advWindowMs}
                onChange={(e) => setAdvWindowMs(Number(e.target.value))}
              />
            </label>
          </div>

          <label className="checkbox-row">
            <input
              type="checkbox"
              checked={reversedCct}
              onChange={(e) => setReversedCct(e.target.checked)}
            />
            <span>Reverse Warm / Cool white channels</span>
          </label>

          <label className="checkbox-row">
            <input
              type="checkbox"
              checked={advEndMarker}
              onChange={(e) => setAdvEndMarker(e.target.checked)}
            />
            <span>Transmit xLamp end-marker burst ([0xBBAA, 0x3412])</span>
          </label>

          <button className="btn btn-primary" type="submit" disabled={!connected || saving}>
            {saving ? 'Saving...' : 'Save Device Settings'}
          </button>
        </form>
      </section>

      <section className="card">
        <div className="card-header">
          <div>
            <h3>Wi-Fi Credentials</h3>
            <p className="muted">Update ESP32 station Wi-Fi network stored in NVS.</p>
          </div>
        </div>
        <form onSubmit={handleSaveWifi} className="form-grid">
          <div className="form-split-2">
            <label>
              <span>Wi-Fi SSID</span>
              <input
                type="text"
                value={wifiSsid}
                onChange={(e) => setWifiSsid(e.target.value)}
                placeholder="Home Wi-Fi name"
                maxLength={32}
              />
            </label>
            <label>
              <span>Wi-Fi Password</span>
              <input
                type="password"
                value={wifiPass}
                onChange={(e) => setWifiPass(e.target.value)}
                placeholder="Password"
                maxLength={64}
              />
            </label>
          </div>
          <button
            className="btn btn-secondary"
            type="submit"
            disabled={!connected || saving || !wifiSsid.trim()}
          >
            Save &amp; Reconnect Wi-Fi
          </button>
        </form>
      </section>
    </div>
  );
};

