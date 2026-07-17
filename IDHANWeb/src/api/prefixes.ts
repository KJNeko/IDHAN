/**
 * Root path prefixes owned by the IDHAN API rather than the WebUI.
 *
 * Drives the Vite dev proxy, so that in development the browser only ever talks to the dev server
 * and every API call stays same-origin — matching production, with no CORS involved. (The credential
 * rides in a header, not a cookie, so same-origin is not strictly required, but keeping it avoids
 * CORS entirely and mirrors prod.)
 *
 * WARNING: mirrored from IDHANServer/src/api/apiPrefixes.hpp, where the same list keeps the SPA
 * history fallback from swallowing API 404s. Keep both in sync; a C++ test asserts they agree.
 */
export const API_PREFIXES = [
  '/api',
  '/auth',
  '/clusters',
  '/db',
  '/file',
  '/generate_api_key',
  '/health',
  '/heartbeat',
  '/hyapi',
  '/integrity',
  '/jobs',
  '/layouts',
  '/log',
  '/mime',
  '/plugins',
  '/purge',
  '/records',
  '/relationships',
  '/search',
  '/tags',
  '/test',
  '/version',
] as const;

/** The only prefix served over WebSocket rather than HTTP. */
export const WS_PREFIXES: readonly string[] = ['/heartbeat'];
