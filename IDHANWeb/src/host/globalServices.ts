/**
 * App-global host services shared by every panel instance: the selection bus, the message bus, the
 * theme signal, and the toast stream. Created once; per-instance hosts (createHost) bind these plus
 * an instance-scoped settings object.
 */

import type { RecordId, ThemeMode, ToastOptions, Unsubscribe } from './types';

function createSelection() {
  let ids: readonly RecordId[] = [];
  const listeners = new Set<(ids: readonly RecordId[]) => void>();
  return {
    get: (): readonly RecordId[] => ids,
    set: (next: readonly RecordId[]): void => {
      ids = [...next];
      for (const listener of listeners) listener(ids);
    },
    subscribe: (listener: (ids: readonly RecordId[]) => void): Unsubscribe => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}

function createBus() {
  const topics = new Map<string, Set<(payload: unknown) => void>>();
  return {
    emit: (topic: string, payload: unknown): void => {
      const set = topics.get(topic);
      if (set) for (const listener of set) listener(payload);
    },
    on: (topic: string, listener: (payload: unknown) => void): Unsubscribe => {
      let set = topics.get(topic);
      if (!set) {
        set = new Set();
        topics.set(topic, set);
      }
      set.add(listener);
      return () => {
        set.delete(listener);
      };
    },
  };
}

function createTheme() {
  const query = window.matchMedia('(prefers-color-scheme: light)');
  const listeners = new Set<(mode: ThemeMode) => void>();
  const mode = (): ThemeMode => (query.matches ? 'light' : 'dark');
  query.addEventListener('change', () => {
    const current = mode();
    for (const listener of listeners) listener(current);
  });
  return {
    mode,
    subscribe: (listener: (mode: ThemeMode) => void): Unsubscribe => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}

export interface Toast {
  id: number;
  message: string;
  kind: NonNullable<ToastOptions['kind']>;
  timeoutMs: number;
}

function createToasts() {
  let nextId = 1;
  const listeners = new Set<(toast: Toast) => void>();
  return {
    push: (message: string, options?: ToastOptions): void => {
      const toast: Toast = {
        id: nextId++,
        message,
        kind: options?.kind ?? 'info',
        timeoutMs: options?.timeoutMs ?? 4000,
      };
      for (const listener of listeners) listener(toast);
    },
    subscribe: (listener: (toast: Toast) => void): Unsubscribe => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}

export const globalServices = {
  selection: createSelection(),
  bus: createBus(),
  theme: createTheme(),
  toasts: createToasts(),
};
