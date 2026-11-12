import React, { useEffect, useState } from 'react';
import { lampApi } from '../services/api';

interface PairingModalProps {
  open: boolean;
  lightName: string;
  paired: boolean;
  onClose: () => void;
  onPairedUpdated: (paired: boolean, statusMsg: string) => void;
}

export const PairingModal: React.FC<PairingModalProps> = ({
  open,
  lightName,
  paired,
  onClose,
  onPairedUpdated,
}) => {
  const [step, setStep] = useState<'ready' | 'sent'>('ready');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [countdown, setCountdown] = useState<number | null>(null);

  useEffect(() => {
    if (!open) {
      setStep('ready');
      setError(null);
      setCountdown(null);
      return;
    }
  }, [open]);

  useEffect(() => {
    if (countdown === null || countdown <= 0) return;
    const t = setTimeout(() => setCountdown((c) => (c !== null ? c - 1 : null)), 1000);
    return () => clearTimeout(t);
  }, [countdown]);

  if (!open) return null;

  const startWindowTimer = () => setCountdown(5);

  const handleSendPair = async () => {
    setBusy(true);
    setError(null);
    try {
      await lampApi.pair();
      setStep('sent');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to transmit pair command');
    } finally {
      setBusy(false);
    }
  };

  const handleConfirmPaired = async () => {
    setBusy(true);
    setError(null);
    try {
      await lampApi.updateDeviceConfig({ paired: true });
      onPairedUpdated(true, 'Pairing confirmed and saved to ESP32');
      onClose();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to save pairing status');
    } finally {
      setBusy(false);
    }
  };

  const handleUnpair = async () => {
    setBusy(true);
    setError(null);
    try {
      await lampApi.unpair();
      onPairedUpdated(false, 'Unpair command transmitted');
      setStep('ready');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to send unpair command');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-sheet" onClick={(e) => e.stopPropagation()} role="dialog" aria-modal="true">
        <div className="modal-header">
          <div>
            <h2>Pair {lightName}</h2>
            <p className="muted">BLE advertising pairing requires the 5-second power-on window.</p>
          </div>
          <button className="icon-btn" onClick={onClose} aria-label="Close pairing dialog">
            ✕
          </button>
        </div>

        <ol className="pair-steps">
          <li>
            <strong>Switch the lamp OFF</strong> using its physical wall switch or breaker.
          </li>
          <li>
            <strong>Switch the lamp back ON</strong>. The lamp listens for pairing only during the first
            <strong> ~5 seconds</strong> after power-on.
          </li>
          <li>
            Immediately tap <strong>Send Pair Command</strong> below so the ESP32 broadcasts the pairing
            frame.
          </li>
        </ol>

        <div className="pair-timer-row">
          <button className="btn btn-ghost" type="button" onClick={startWindowTimer}>
            {countdown !== null && countdown > 0
              ? `5s Window Timer: ${countdown}s`
              : countdown === 0
              ? 'Window Elapsed — Restart Timer'
              : 'Start 5s Helper Timer'}
          </button>
        </div>

        {error && <div className="alert alert-error">{error}</div>}

        {step === 'ready' ? (
          <div className="pair-actions">
            <button
              className="btn btn-primary btn-lg"
              type="button"
              onClick={handleSendPair}
              disabled={busy}
            >
              {busy ? 'Broadcasting Pair Packet...' : 'Send Pair Command'}
            </button>
            {paired && (
              <button
                className="btn btn-danger-outline"
                type="button"
                onClick={handleUnpair}
                disabled={busy}
              >
                Unpair Lamp
              </button>
            )}
          </div>
        ) : (
          <div className="pair-confirm-box">
            <p className="pair-confirm-title">
              Pairing packet transmitted. Did the lamp flash or beep?
            </p>
            <p className="muted">
              BLE advertising is one-way, so the lamp cannot send an acknowledgment back to the ESP32.
            </p>
            <div className="pair-actions">
              <button
                className="btn btn-primary"
                type="button"
                onClick={handleConfirmPaired}
                disabled={busy}
              >
                {busy ? 'Saving...' : 'Yes, Lamp Flashed — Confirm Paired'}
              </button>
              <button
                className="btn btn-secondary"
                type="button"
                onClick={() => {
                  setStep('ready');
                  setError(null);
                }}
                disabled={busy}
              >
                No Response — Retry
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};

