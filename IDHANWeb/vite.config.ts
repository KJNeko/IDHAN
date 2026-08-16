import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { API_PREFIXES, WS_PREFIXES } from './src/api/prefixes';

const SERVER_TARGET = process.env.IDHAN_DEV_TARGET ?? 'http://dev.idhan:16609';

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
