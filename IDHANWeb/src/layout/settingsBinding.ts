/**
 * Binds a panel instance's SettingsApi to the layout store. get/set read and write that instance's
 * config entry; subscribe fires when it changes (e.g. the panel itself, or a layout load).
 */

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
