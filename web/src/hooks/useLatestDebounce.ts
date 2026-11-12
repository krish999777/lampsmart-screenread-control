import { useCallback, useEffect, useRef } from 'react';

/**
 * Debounces rapid slider updates while guaranteeing the latest final value
 * is flushed immediately on release (`flush`).
 */
export function useLatestDebounce<T extends (...args: never[]) => void>(
  callback: T,
  delayMs: number
) {
  const cbRef = useRef(callback);
  cbRef.current = callback;

  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pendingArgsRef = useRef<Parameters<T> | null>(null);

  const cancel = useCallback(() => {
    if (timerRef.current !== null) {
      clearTimeout(timerRef.current);
      timerRef.current = null;
    }
  }, []);

  const schedule = useCallback(
    (...args: Parameters<T>) => {
      pendingArgsRef.current = args;
      cancel();
      timerRef.current = setTimeout(() => {
        timerRef.current = null;
        const latest = pendingArgsRef.current;
        pendingArgsRef.current = null;
        if (latest) {
          cbRef.current(...latest);
        }
      }, delayMs);
    },
    [cancel, delayMs]
  );

  const flush = useCallback(
    (...overrideArgs: Parameters<T>) => {
      cancel();
      pendingArgsRef.current = null;
      cbRef.current(...overrideArgs);
    },
    [cancel]
  );

  useEffect(() => cancel, [cancel]);

  return { schedule, flush };
}

