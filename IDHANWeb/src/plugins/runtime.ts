/**
 * Installs the shared singletons that plugin bundles resolve to through the page's import map.
 *
 * Plugins import bare `react` / `react-dom` / `react/jsx-runtime` / `@idhan/host`; the import map
 * points those at public shim modules (public/idhan-runtime/*.mjs) which read
 * `window.__IDHAN_RUNTIME__`. This fills that object with the host's own React instance, so a plugin
 * never loads a second copy and breaks hooks. Call once, before any plugin can load.
 */

import * as React from 'react';
import * as ReactJsxRuntime from 'react/jsx-runtime';
import * as ReactDom from 'react-dom';
import { registerPanel } from '../panels/registry';
import type { PanelDefinition } from '../host/types';
import { HOST_API_VERSION } from '../host/types';

interface IdhanRuntime {
  react: typeof React;
  reactJsxRuntime: typeof ReactJsxRuntime;
  reactDom: typeof ReactDom;
  host: {
    registerPanel: (definition: PanelDefinition) => void;
    HOST_API_VERSION: string;
  };
}

declare global {
  interface Window {
    __IDHAN_RUNTIME__?: IdhanRuntime;
  }
}

/**
 * Plugin-facing panel registration. Where the registry's registerPanel throws on a duplicate type, a
 * colliding plugin is warned and skipped rather than crashing the load.
 */
function registerPluginPanel(definition: PanelDefinition): void {
  try {
    registerPanel(definition);
  } catch (error) {
    console.warn(`[idhan] plugin panel "${definition?.type}" was not registered:`, error);
  }
}

let installed = false;

export function installPluginRuntime(): void {
  if (installed) return;
  installed = true;
  window.__IDHAN_RUNTIME__ = {
    react: React,
    reactJsxRuntime: ReactJsxRuntime,
    reactDom: ReactDom,
    host: { registerPanel: registerPluginPanel, HOST_API_VERSION },
  };
}
