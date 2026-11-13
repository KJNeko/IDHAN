/**
 * Runtime shim for `@idhan/host`, the surface a plugin imports to register its panels. Types are
 * compile-time only, from the @idhan/host type package, so the runtime surface here is just what a
 * bundle actually calls. See react.mjs for how the import map points here.
 */
const host = globalThis.__IDHAN_RUNTIME__?.host;
if (!host) throw new Error('[idhan] host runtime not installed before a plugin imported "@idhan/host"');

export const registerPanel = host.registerPanel;

export const HOST_API_VERSION = host.HOST_API_VERSION;
