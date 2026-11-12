import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { ColorWheel } from './components/ColorWheel';
import { PairingModal } from './components/PairingModal';
import { SettingsView } from './components/SettingsView';
import { useLatestDebounce } from './hooks/useLatestDebounce';
import { lampApi } from './services/api';
import type { EspStatusResponse, LampStateResponse } from './types/lamp';
import {
  XLAMP_COLOR_PRESETS,
  cctPreviewColor,
  hexToRgb,
  hsvToRgb,
  rgbToHex,
  rgbToHsv,
} from './utils/color';

type MainTab = 'control' | 'settings';
type LightModeTab = 'white' | 'color' | 'scenes';

export function App() {
  const [tab, setTab] = useState<MainTab>('control');
  const [modeTab, setModeTab] = useState<LightModeTab>('white');
  const [status, setStatus] = useState<EspStatusResponse | null>(null);
  const [connected, setConnected] = useState(false);
  const [pairingOpen, setPairingOpen] = useState(false);

  // Local requested state synced from ESP32 /api/v1/state
  const [power, setPower] = useState(false);
  const [brightness, setBrightness] = useState(80);
  const [temperature, setTemperature] = useState(50);
  const [hue, setHue] = useState(35);
  const [sat, setSat] = useState(35);
  const [val, setVal] = useState(100);
  const [activeScene, setActiveScene] = useState(1);
  const [colorMode, setColorMode] = useState<'cct' | 'rgb' | 'scene' | 'night'>('cct');

  const [rgbDraft, setRgbDraft] = useState({ r: '255', g: '214', b: '170' });
  const [toast, setToast] = useState<{ text: string; isError: boolean } | null>(null);
  const [busyAction, setBusyAction] = useState<string | null>(null);

  const notify = useCallback((text: string, isError = false) => {
    setToast({ text, isError });
  }, []);

  useEffect(() => {
    if (!toast) return;
    const t = setTimeout(() => setToast(null), 3500);
    return () => clearTimeout(t);
  }, [toast]);

  const applyServerState = useCallback((st: LampStateResponse) => {
    setPower(st.power);
    setBrightness(st.brightness);
    setTemperature(st.temperature);
    setActiveScene(st.scene);
    setColorMode(st.colorMode);
    if (st.color) {
      const hsv = rgbToHsv(st.color.r, st.color.g, st.color.b);
      setHue(st.color.h || hsv.h);
      setSat(st.color.s || hsv.s);
      setVal(st.color.v || hsv.v || 100);
      setRgbDraft({
        r: String(st.color.r),
        g: String(st.color.g),
        b: String(st.color.b),
      });
    }
    if (st.colorMode === 'rgb') setModeTab('color');
    else if (st.colorMode === 'scene') setModeTab('scenes');
    else setModeTab('white');
  }, []);

  const refreshAll = useCallback(async () => {
    try {
      const [stRes, stateRes] = await Promise.all([lampApi.getStatus(), lampApi.getState()]);
      setStatus(stRes);
      setConnected(true);
      applyServerState(stateRes);
    } catch {
      setConnected(false);
    }
  }, [applyServerState]);

  const refreshStatusOnly = useCallback(async () => {
    try {
      const stRes = await lampApi.getStatus();
      setStatus(stRes);
      setConnected(true);
    } catch {
      setConnected(false);
    }
  }, []);

  useEffect(() => {
    void refreshAll();
    const timer = setInterval(() => {
      void refreshStatusOnly();
    }, 8000);
    return () => clearInterval(timer);
  }, [refreshAll, refreshStatusOnly]);

  // Debounced transmitter helpers (latest-value-wins)
  const sendBrightness = useLatestDebounce(async (value: number) => {
    try {
      const res = await lampApi.setBrightness(value);
      setPower(true);
      setColorMode('cct');
      if (res.frameHex) void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'Brightness update failed', true);
    }
  }, 120);

  const sendTemperature = useLatestDebounce(async (value: number) => {
    try {
      const res = await lampApi.setTemperature(value);
      setPower(true);
      setColorMode('cct');
      if (res.frameHex) void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'Temperature update failed', true);
    }
  }, 120);

  const sendHsvColor = useLatestDebounce(async (h: number, s: number, v: number) => {
    try {
      const res = await lampApi.setColorHsv(h, s, v);
      setPower(true);
      setColorMode('rgb');
      if (res.frameHex) void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'Color command failed', true);
    }
  }, 130);

  const currentRgb = useMemo(() => hsvToRgb(hue, sat, val), [hue, sat, val]);
  const currentHex = useMemo(() => rgbToHex(currentRgb), [currentRgb]);

  useEffect(() => {
    setRgbDraft({
      r: String(currentRgb.r),
      g: String(currentRgb.g),
      b: String(currentRgb.b),
    });
  }, [currentRgb]);

  const supportsRgb = status?.protocol.supportsRgb ?? true;
  const supportsScenes = status?.protocol.supportsScenes ?? true;
  const lightName = status?.device.lightName || 'LampSmart Light';

  const orbColor = useMemo(() => {
    if (!power) return 'rgba(148, 163, 184, 0.25)';
    if (colorMode === 'rgb') return `rgb(${currentRgb.r}, ${currentRgb.g}, ${currentRgb.b})`;
    if (colorMode === 'night') return 'rgb(129, 140, 248)';
    if (colorMode === 'scene') return 'rgb(244, 114, 182)';
    return cctPreviewColor(temperature);
  }, [power, colorMode, currentRgb, temperature]);

  const handleTogglePower = async () => {
    const next = !power;
    setBusyAction('power');
    try {
      await lampApi.setPower(next);
      setPower(next);
      void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'Power command failed', true);
    } finally {
      setBusyAction(null);
    }
  };

  const handleBrightnessStep = (nextVal: number) => {
    const clamped = Math.max(1, Math.min(100, Math.round(nextVal)));
    setBrightness(clamped);
    sendBrightness.flush(clamped);
  };

  const handlePresetColor = async (hex: string) => {
    const rgb = hexToRgb(hex);
    const hsv = rgbToHsv(rgb.r, rgb.g, rgb.b);
    setHue(hsv.h);
    setSat(hsv.s);
    setVal(hsv.v);
    setColorMode('rgb');
    setPower(true);
    try {
      await lampApi.setColorRgb(rgb.r, rgb.g, rgb.b, brightness);
      void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'Preset color failed', true);
    }
  };

  const handleApplyRgbInputs = async (e: React.FormEvent) => {
    e.preventDefault();
    const r = Math.max(0, Math.min(255, Number(rgbDraft.r) || 0));
    const g = Math.max(0, Math.min(255, Number(rgbDraft.g) || 0));
    const b = Math.max(0, Math.min(255, Number(rgbDraft.b) || 0));
    const hsv = rgbToHsv(r, g, b);
    setHue(hsv.h);
    setSat(hsv.s);
    setVal(hsv.v);
    try {
      await lampApi.setColorRgb(r, g, b, brightness);
      setPower(true);
      setColorMode('rgb');
      void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'RGB command failed', true);
    }
  };

  const handleScene = async (sceneNum: number) => {
    setBusyAction(`scene-${sceneNum}`);
    try {
      await lampApi.setScene(sceneNum);
      setActiveScene(sceneNum);
      setPower(true);
      setColorMode('scene');
      void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'Scene command failed', true);
    } finally {
      setBusyAction(null);
    }
  };

  const handleSceneAction = async (action: 'on' | 'reset') => {
    setBusyAction(`scene-${action}`);
    try {
      await lampApi.sceneAction(action);
      if (action === 'on') {
        setPower(true);
        setColorMode('scene');
      }
      notify(action === 'on' ? 'Scene mode activated' : 'Scene reset transmitted');
      void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'Scene action failed', true);
    } finally {
      setBusyAction(null);
    }
  };

  const handleNightLight = async () => {
    setBusyAction('night');
    try {
      await lampApi.nightLight();
      setPower(true);
      setColorMode('night');
      void refreshStatusOnly();
    } catch (err) {
      notify(err instanceof Error ? err.message : 'Night light command failed', true);
    } finally {
      setBusyAction(null);
    }
  };

  return (
    <div className="app-shell">
      <div
        className="ambient-glow"
        style={{
          background: `radial-gradient(circle at 50% 0%, ${orbColor}, transparent 70%)`,
          opacity: power ? 0.35 : 0.12,
        }}
      />

      <header className="topbar">
        <div className="brand-block">
          <div className="brand-title-row">
            <h1>{lightName}</h1>
            <span className={`pill ${connected ? 'pill-ok' : 'pill-err'}`}>
              {connected ? (status?.protocol.id.toUpperCase() ?? 'ESP32') : 'Offline'}
            </span>
          </div>
          <p className="topbar-sub">
            {connected && status
              ? `Last TX: ${status.lastTransmission.command} (${status.lastTransmission.status})`
              : 'ESP32 unreachable — configure IP in Settings'}
          </p>
        </div>

        <nav className="nav-pills" aria-label="Primary views">
          <button
            className={`nav-pill ${tab === 'control' ? 'is-active' : ''}`}
            type="button"
            onClick={() => setTab('control')}
          >
            Control
          </button>
          <button
            className={`nav-pill ${tab === 'settings' ? 'is-active' : ''}`}
            type="button"
            onClick={() => setTab('settings')}
          >
            Settings
          </button>
        </nav>
      </header>

      {toast && (
        <div className={`toast ${toast.isError ? 'toast-err' : 'toast-ok'}`} role="status">
          {toast.text}
        </div>
      )}

      {tab === 'control' ? (
        <main className="control-main">
          {connected && status && !status.device.paired && (
            <div className="pair-banner">
              <div>
                <strong>Light not paired yet</strong>
                <span>Power-cycle your lamp and pair within 5 seconds.</span>
              </div>
              <button className="btn btn-primary" type="button" onClick={() => setPairingOpen(true)}>
                Pair
              </button>
            </div>
          )}

          {connected && status && !status.ble.initialized && (
            <div className="alert alert-error">
              ESP32 BLE stack failed to initialize: {status.ble.error || 'Unknown error'}
            </div>
          )}

          {/* Hero Power Orb */}
          <section className="hero-section">
            <button
              type="button"
              className={`power-orb ${power ? 'is-on' : 'is-off'}`}
              onClick={handleTogglePower}
              disabled={!connected || busyAction === 'power'}
              style={{
                boxShadow: power ? `0 0 48px -6px ${orbColor}` : undefined,
                borderColor: power ? orbColor : undefined,
              }}
              aria-pressed={power}
              aria-label={power ? 'Turn lamp off' : 'Turn lamp on'}
            >
              <svg viewBox="0 0 24 24" className="power-icon" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M12 2v10" strokeLinecap="round" />
                <path d="M18.36 6.64a9 9 0 1 1-12.73 0" strokeLinecap="round" />
              </svg>
            </button>
            <div className="hero-meta">
              <span className="hero-state">{power ? 'Requested ON' : 'Requested OFF'}</span>
              <span className="muted-xs">
                Push-only BLE broadcast · {brightness}% brightness
              </span>
            </div>
          </section>

          {/* Mode Switcher */}
          <div className="segmented" role="tablist">
            <button
              type="button"
              role="tab"
              aria-selected={modeTab === 'white'}
              className={`seg-btn ${modeTab === 'white' ? 'is-active' : ''}`}
              onClick={() => setModeTab('white')}
            >
              White &amp; Dim
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={modeTab === 'color'}
              className={`seg-btn ${modeTab === 'color' ? 'is-active' : ''}`}
              onClick={() => setModeTab('color')}
            >
              Color (RGB)
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={modeTab === 'scenes'}
              className={`seg-btn ${modeTab === 'scenes' ? 'is-active' : ''}`}
              onClick={() => setModeTab('scenes')}
            >
              Scenes
            </button>
          </div>

          {/* Brightness Card (always accessible in White & Color tabs) */}
          {modeTab !== 'scenes' && (
            <section className="card">
              <div className="card-header">
                <h3>Brightness</h3>
                <span className="value-badge">{brightness}%</span>
              </div>

              <div className="slider-row">
                <button
                  type="button"
                  className="step-btn"
                  disabled={!connected}
                  onClick={() => handleBrightnessStep(brightness - 5)}
                  aria-label="Decrease brightness"
                >
                  −
                </button>
                <input
                  type="range"
                  min={1}
                  max={100}
                  value={brightness}
                  disabled={!connected}
                  className="range-slider"
                  onChange={(e) => {
                    const next = Number(e.target.value);
                    setBrightness(next);
                    sendBrightness.schedule(next);
                  }}
                  onPointerUp={(e) => {
                    sendBrightness.flush(Number((e.target as HTMLInputElement).value));
                  }}
                  onKeyUp={(e) => {
                    sendBrightness.flush(Number((e.target as HTMLInputElement).value));
                  }}
                  aria-label="Brightness percentage"
                />
                <button
                  type="button"
                  className="step-btn"
                  disabled={!connected}
                  onClick={() => handleBrightnessStep(brightness + 5)}
                  aria-label="Increase brightness"
                >
                  +
                </button>
              </div>

              <div className="quick-chips">
                <button
                  type="button"
                  className="chip"
                  disabled={!connected}
                  onClick={() => handleBrightnessStep(1)}
                >
                  Min (1%)
                </button>
                <button
                  type="button"
                  className="chip"
                  disabled={!connected}
                  onClick={() => handleBrightnessStep(25)}
                >
                  25%
                </button>
                <button
                  type="button"
                  className="chip"
                  disabled={!connected}
                  onClick={() => handleBrightnessStep(50)}
                >
                  50%
                </button>
                <button
                  type="button"
                  className="chip"
                  disabled={!connected}
                  onClick={() => handleBrightnessStep(100)}
                >
                  Max (100%)
                </button>
              </div>
            </section>
          )}

          {/* White / Color Temperature Card */}
          {modeTab === 'white' && (
            <section className="card">
              <div className="card-header">
                <h3>Color Temperature</h3>
                <span className="value-badge">
                  {temperature < 33 ? `Warm (${temperature})` : temperature < 67 ? `Neutral (${temperature})` : `Cool (${temperature})`}
                </span>
              </div>

              <input
                type="range"
                min={0}
                max={100}
                value={temperature}
                disabled={!connected}
                className="range-slider cct-slider"
                onChange={(e) => {
                  const next = Number(e.target.value);
                  setTemperature(next);
                  sendTemperature.schedule(next);
                }}
                onPointerUp={(e) => {
                  sendTemperature.flush(Number((e.target as HTMLInputElement).value));
                }}
                onKeyUp={(e) => {
                  sendTemperature.flush(Number((e.target as HTMLInputElement).value));
                }}
                aria-label="Color temperature warm to cool"
              />

              <div className="cct-labels">
                <span>Warm (0)</span>
                <span>Neutral (50)</span>
                <span>Cool (100)</span>
              </div>

              <div className="quick-chips">
                {[
                  { label: 'Warm', val: 0 },
                  { label: 'Soft', val: 25 },
                  { label: 'Neutral', val: 50 },
                  { label: 'Daylight', val: 75 },
                  { label: 'Cool', val: 100 },
                ].map((preset) => (
                  <button
                    key={preset.label}
                    type="button"
                    className={`chip ${Math.abs(temperature - preset.val) <= 4 ? 'is-selected' : ''}`}
                    disabled={!connected}
                    onClick={() => {
                      setTemperature(preset.val);
                      sendTemperature.flush(preset.val);
                    }}
                  >
                    {preset.label}
                  </button>
                ))}
              </div>
            </section>
          )}

          {/* RGB / HSV Color Card */}
          {modeTab === 'color' && (
            <section className="card">
              <div className="card-header">
                <div>
                  <h3>Color Selection</h3>
                  <p className="muted">HSV Color Wheel, RGB inputs &amp; 12 presets</p>
                </div>
                <div className="color-preview-pill">
                  <span className="swatch-dot" style={{ backgroundColor: currentHex }} />
                  <code>{currentHex}</code>
                </div>
              </div>

              {!supportsRgb && (
                <div className="alert alert-warn">
                  Selected protocol ({status?.protocol.id}) is CCT-only and does not support RGB.
                  Switch to LampSmart Pro V3 in Settings if your lamp supports RGB.
                </div>
              )}

              <div className="color-wheel-center">
                <ColorWheel
                  hue={hue}
                  saturation={sat}
                  disabled={!connected || !supportsRgb}
                  onChange={(h, s) => {
                    setHue(h);
                    setSat(s);
                    sendHsvColor.schedule(h, s, val);
                  }}
                  onChangeEnd={(h, s) => {
                    setHue(h);
                    setSat(s);
                    sendHsvColor.flush(h, s, val);
                  }}
                />
              </div>

              <div className="hsv-sliders">
                <label className="slider-field">
                  <div className="slider-label">
                    <span>Saturation</span>
                    <span>{sat}%</span>
                  </div>
                  <input
                    type="range"
                    min={0}
                    max={100}
                    value={sat}
                    disabled={!connected || !supportsRgb}
                    className="range-slider"
                    onChange={(e) => {
                      const nextS = Number(e.target.value);
                      setSat(nextS);
                      sendHsvColor.schedule(hue, nextS, val);
                    }}
                    onPointerUp={(e) =>
                      sendHsvColor.flush(hue, Number((e.target as HTMLInputElement).value), val)
                    }
                  />
                </label>

                <label className="slider-field">
                  <div className="slider-label">
                    <span>Color Brightness (V)</span>
                    <span>{val}%</span>
                  </div>
                  <input
                    type="range"
                    min={1}
                    max={100}
                    value={val}
                    disabled={!connected || !supportsRgb}
                    className="range-slider"
                    onChange={(e) => {
                      const nextV = Number(e.target.value);
                      setVal(nextV);
                      sendHsvColor.schedule(hue, sat, nextV);
                    }}
                    onPointerUp={(e) =>
                      sendHsvColor.flush(hue, sat, Number((e.target as HTMLInputElement).value))
                    }
                  />
                </label>
              </div>

              <form className="rgb-input-row" onSubmit={handleApplyRgbInputs}>
                <label>
                  <span>R</span>
                  <input
                    type="number"
                    min={0}
                    max={255}
                    value={rgbDraft.r}
                    disabled={!connected || !supportsRgb}
                    onChange={(e) => setRgbDraft((d) => ({ ...d, r: e.target.value }))}
                  />
                </label>
                <label>
                  <span>G</span>
                  <input
                    type="number"
                    min={0}
                    max={255}
                    value={rgbDraft.g}
                    disabled={!connected || !supportsRgb}
                    onChange={(e) => setRgbDraft((d) => ({ ...d, g: e.target.value }))}
                  />
                </label>
                <label>
                  <span>B</span>
                  <input
                    type="number"
                    min={0}
                    max={255}
                    value={rgbDraft.b}
                    disabled={!connected || !supportsRgb}
                    onChange={(e) => setRgbDraft((d) => ({ ...d, b: e.target.value }))}
                  />
                </label>
                <button
                  type="submit"
                  className="btn btn-secondary"
                  disabled={!connected || !supportsRgb}
                >
                  Apply RGB
                </button>
              </form>

              <div className="preset-grid" aria-label="12 Color Presets">
                {XLAMP_COLOR_PRESETS.map((preset) => {
                  const selected = currentHex === preset.hex.toUpperCase() && colorMode === 'rgb';
                  return (
                    <button
                      key={preset.hex}
                      type="button"
                      className={`preset-swatch ${selected ? 'is-selected' : ''}`}
                      style={{ backgroundColor: preset.hex }}
                      title={preset.name}
                      aria-label={preset.name}
                      disabled={!connected || !supportsRgb}
                      onClick={() => handlePresetColor(preset.hex)}
                    />
                  );
                })}
              </div>
            </section>
          )}

          {/* Scenes & Night Light Card */}
          {modeTab === 'scenes' && (
            <section className="card">
              <div className="card-header">
                <div>
                  <h3>Scenes &amp; Modes</h3>
                  <p className="muted">Scene effects are defined by the lamp fixture firmware.</p>
                </div>
              </div>

              {!supportsScenes && (
                <div className="alert alert-warn">
                  Legacy V1 protocol supports Night Light only; Scenes 1–4 require V2/V3.
                </div>
              )}

              <div className="scene-grid">
                {[1, 2, 3, 4].map((num) => (
                  <button
                    key={num}
                    type="button"
                    className={`scene-tile ${colorMode === 'scene' && activeScene === num ? 'is-active' : ''}`}
                    disabled={!connected || !supportsScenes || busyAction === `scene-${num}`}
                    onClick={() => handleScene(num)}
                  >
                    <span className="scene-num">{num}</span>
                    <span>Scene {num}</span>
                  </button>
                ))}
              </div>

              <div className="scene-actions">
                <button
                  type="button"
                  className="btn btn-secondary"
                  disabled={!connected || !supportsScenes || busyAction === 'scene-on'}
                  onClick={() => handleSceneAction('on')}
                >
                  Scene On
                </button>
                <button
                  type="button"
                  className={`btn ${colorMode === 'night' ? 'btn-primary' : 'btn-secondary'}`}
                  disabled={!connected || busyAction === 'night'}
                  onClick={handleNightLight}
                >
                  Night Light
                </button>
                <button
                  type="button"
                  className="btn btn-ghost"
                  disabled={!connected || !supportsScenes || busyAction === 'scene-reset'}
                  onClick={() => handleSceneAction('reset')}
                >
                  Scene Reset
                </button>
              </div>
            </section>
          )}
        </main>
      ) : (
        <main className="control-main">
          <SettingsView
            status={status}
            connected={connected}
            onRefresh={refreshAll}
            onOpenPairing={() => setPairingOpen(true)}
            onNotify={notify}
          />
        </main>
      )}

      <PairingModal
        open={pairingOpen}
        lightName={lightName}
        paired={Boolean(status?.device.paired)}
        onClose={() => setPairingOpen(false)}
        onPairedUpdated={async (_paired, msg) => {
          notify(msg);
          await refreshAll();
        }}
      />
    </div>
  );
}

export default App;

