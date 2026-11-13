import type { SettingsApi } from '../host/types';
import { useLayoutStore } from './store';

export function makeSettingsBinding(instanceId: string): SettingsApi {
  const read = () => useLayoutStore.getState().getPanelConfig(instanceId);
  return {
    get: () => read(),
    set: (patch) => useLayoutStore.getState().setPanelConfig(instanceId, patch as Record<string, unknown>),
    subscribe: (listener) => {
      let previous = read();
      return useLayoutStore.subscribe(() => {
        const next = read();
        if (next !== previous) {
          previous = next;
          listener(next);
        }
      });
    },
  };
}
