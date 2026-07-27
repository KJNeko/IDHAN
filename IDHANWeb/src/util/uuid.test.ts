import { afterEach, describe, expect, it, vi } from 'vitest';
import { uuid } from './uuid';

const V4 = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

describe('uuid', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('produces a well-formed v4 uuid', () => {
    expect(uuid()).toMatch(V4);
  });

  it('returns distinct values', () => {
    expect(uuid()).not.toBe(uuid());
  });

  it('falls back to getRandomValues when randomUUID is missing (insecure context)', () => {
    // Simulate plain-HTTP remote access: randomUUID is undefined but getRandomValues still works.
    vi.spyOn(globalThis.crypto, 'randomUUID').mockImplementation(() => {
      throw new TypeError('crypto.randomUUID is not a function');
    });
    // The helper checks typeof, so also hide it entirely.
    const original = globalThis.crypto.randomUUID;
    // @ts-expect-error deliberately removing to mimic an insecure context
    globalThis.crypto.randomUUID = undefined;
    try {
      const id = uuid();
      expect(id).toMatch(V4);
    } finally {
      globalThis.crypto.randomUUID = original;
    }
  });
});
