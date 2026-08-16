import { describe, expect, it } from 'vitest';
import { satisfiesHostApi } from './semver';

describe('satisfiesHostApi', () => {
  it('accepts a caret range within the same major', () => {
    expect(satisfiesHostApi('1.4.2', '^1.0.0')).toBe(true);
    expect(satisfiesHostApi('1.0.0', '^1.0.0')).toBe(true);
    expect(satisfiesHostApi('1.9.9', '^1.2.0')).toBe(true);
  });

  it('rejects a caret range below the wanted version or across a major', () => {
    expect(satisfiesHostApi('1.1.0', '^1.2.0')).toBe(false); // host older than wanted
    expect(satisfiesHostApi('2.0.0', '^1.0.0')).toBe(false); // next major
    expect(satisfiesHostApi('0.9.0', '^1.0.0')).toBe(false); // previous major
  });

  it('tightens caret to the minor before 1.0', () => {
    expect(satisfiesHostApi('0.2.5', '^0.2.0')).toBe(true);
    expect(satisfiesHostApi('0.3.0', '^0.2.0')).toBe(false);
  });

  it('honours exact and wildcard ranges', () => {
    expect(satisfiesHostApi('1.2.3', '1.2.3')).toBe(true);
    expect(satisfiesHostApi('1.2.4', '1.2.3')).toBe(false);
    expect(satisfiesHostApi('7.0.1', '*')).toBe(true);
  });

  it('fails closed on garbage input', () => {
    expect(satisfiesHostApi('1.0.0', 'not-a-range')).toBe(false);
    expect(satisfiesHostApi('nonsense', '^1.0.0')).toBe(false);
  });
});
