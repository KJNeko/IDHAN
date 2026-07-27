import { describe, expect, it } from 'vitest';
import * as React from 'react';
import { installPluginRuntime } from './runtime';
import { HOST_API_VERSION } from '../host/types';

describe('installPluginRuntime', () => {
  it('exposes the host React instance and registration on window', () => {
    installPluginRuntime();
    const rt = window.__IDHAN_RUNTIME__;

    expect(rt).toBeDefined();
    // Same reference the shims re-export — a plugin therefore shares the host's single React.
    expect(rt?.react).toBe(React);
    expect(typeof rt?.host.registerPanel).toBe('function');
    expect(rt?.host.HOST_API_VERSION).toBe(HOST_API_VERSION);
  });
});
