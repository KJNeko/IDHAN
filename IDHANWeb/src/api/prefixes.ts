/**
 * Root path prefixes owned by the IDHAN API rather than the WebUI.
 *
 * Drives the Vite dev proxy, so that in development the browser only ever talks to the dev server
 * and every API call stays same-origin, matching production with no CORS involved.
 *
 * WARNING: mirrored from IDHANServer/src/api/apiPrefixes.hpp, where the same list keeps the SPA
 * history fallback from swallowing API 404s. Keep both in sync; a C++ test asserts they agree.
 */
export const API_PREFIXES = [
  '/api',
  '/auth',
  '/clusters',
  '/db',
    '/download_sessions',
    '/downloader',
    '/embeddings',
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
    '/rate_limits',
  '/records',
  '/relationships',
  '/search',
  '/tags',
  '/test',
  '/version',
] as const;

/** Prefixes that also carry WebSocket traffic, which the dev proxy has to upgrade. */
export const WS_PREFIXES: readonly string[] = ['/heartbeat', '/download_sessions', '/rate_limits'];
