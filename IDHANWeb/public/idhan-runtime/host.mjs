/**
 * Runtime shim for `@idhan/host` — the surface a plugin imports to register its panels. Types are
 * compile-time only (a plugin depends on the @idhan/host type package); the runtime surface here is
 * just what a bundle actually calls. See react.mjs for how the import map points here.
 */
const host = globalThis.__IDHAN_RUNTIME__?.host;
if (!host) throw new Error('[idhan] host runtime not installed before a plugin imported "@idhan/host"');

/** Register a PanelDefinition into the catalog. Duplicate/invalid types are warned and skipped. */
export const registerPanel = host.registerPanel;

/** The host API version this build implements (a plugin manifest declares a compatible range). */
export const HOST_API_VERSION = host.HOST_API_VERSION;
