import { afterEach, describe, expect, it, vi } from 'vitest';

// Only `plugins.list` is used by the loader; mock the module to that.
const mocks = vi.hoisted(() => ({ list: vi.fn() }));
vi.mock('../api/client', () => ({ plugins: { list: mocks.list } }));

afterEach(() => {
  vi.resetModules(); // reset the loader's memoized promise between tests
  vi.clearAllMocks();
});

describe('loadPlugins', () => {
  it('returns 0 and does not throw when the plugin index fetch fails', async () => {
    mocks.list.mockRejectedValue(new Error('network down'));
    const { loadPlugins } = await import('./loader');
    await expect(loadPlugins()).resolves.toBe(0);
  });

  it('skips a plugin whose declared host API is incompatible', async () => {
    mocks.list.mockResolvedValue([
      { id: 'too-new', name: 'X', version: '1.0.0', hostApi: '^99.0.0', dir: 'x', entry: '/plugins/x/index.js' },
    ]);
    const { loadPlugins } = await import('./loader');
    // Incompatible → never imported → nothing loaded, and no throw.
    await expect(loadPlugins()).resolves.toBe(0);
  });
});
