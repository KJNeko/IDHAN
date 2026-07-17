import { defineConfig } from 'vitest/config';

// jsdom gives the store tests localStorage and crypto.randomUUID without a real browser.
export default defineConfig({
  test: {
    environment: 'jsdom',
    include: ['src/**/*.test.{ts,tsx}'],
    setupFiles: ['src/test/setup.ts'],
  },
});
