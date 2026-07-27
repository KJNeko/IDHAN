/**
 * Loads third-party panel plugins.
 *
 * Flow: install the shared runtime → fetch the server's plugin index → for each plugin whose declared
 * host-API range is compatible, dynamic-import its bundle. The bundle self-registers its panels through
 * `@idhan/host` (the same registerPanel path the built-ins use). A plugin that is incompatible or fails
 * to import is warned and skipped — a bad plugin never blocks the app or the other plugins.
 *
 * The dynamic import below carries a `@vite-ignore` leading-comment so Vite/Rollup emits a native
 * dynamic import instead of trying to bundle the runtime-only, server-provided bundle URL.
 */

import { plugins as pluginApi } from '../api/client';
import type { PluginManifest } from '../api/types';
import { HOST_API_VERSION } from '../host/types';
import { installPluginRuntime } from './runtime';
import { satisfiesHostApi } from './semver';

let loadPromise: Promise<number> | null = null;

/** Idempotent: the first call kicks off loading; later calls await the same result. */
export function loadPlugins(): Promise<number> {
  loadPromise ??= doLoad();
  return loadPromise;
}

async function doLoad(): Promise<number> {
  // Must be in place before any plugin bundle resolves `react` / `@idhan/host` through the import map.
  installPluginRuntime();

  let manifests: PluginManifest[];
  try {
    manifests = await pluginApi.list();
  } catch (error) {
    console.warn('[idhan] could not fetch the plugin index; no plugins loaded:', error);
    return 0;
  }

  let loaded = 0;
  for (const manifest of manifests) {
    if (!satisfiesHostApi(HOST_API_VERSION, manifest.hostApi)) {
      console.warn(
        `[idhan] skipping plugin "${manifest.id}": needs host API ${manifest.hostApi}, host provides ${HOST_API_VERSION}`,
      );
      continue;
    }
    try {
      await import(/* @vite-ignore */ manifest.entry);
      loaded += 1;
    } catch (error) {
      console.warn(`[idhan] plugin "${manifest.id}" failed to load from ${manifest.entry}:`, error);
    }
  }

  if (loaded > 0) console.info(`[idhan] loaded ${loaded} plugin(s)`);
  return loaded;
}
