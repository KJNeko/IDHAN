import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { API_PREFIXES, WS_PREFIXES } from './src/api/prefixes';

const SERVER_TARGET = process.env.IDHAN_DEV_TARGET ?? 'http://127.0.0.1:16609';

// Proxy every API prefix to the running IDHANServer. The browser then only ever talks to the dev
// server, so requests are same-origin exactly as they are in production: the session cookie works
// identically in both, and CORS never enters the picture. `changeOrigin` stays false so the Origin
// header the server sees matches what it would see in production.
const proxy = Object.fromEntries(
  API_PREFIXES.map((prefix) => [
    prefix,
    { target: SERVER_TARGET, changeOrigin: false, ws: WS_PREFIXES.includes(prefix) },
  ]),
);

export default defineConfig({
  plugins: [react()],
  server: { port: 5173, proxy },
  build: { outDir: 'dist', emptyOutDir: true, sourcemap: true },
});
