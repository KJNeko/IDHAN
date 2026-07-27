/**
 * A v4 UUID that works outside secure contexts.
 *
 * `crypto.randomUUID()` is only defined in a secure context (HTTPS or localhost). A self-hosted IDHAN
 * reached over plain HTTP on a LAN/remote address is *not* secure, so `randomUUID` is undefined there
 * and calling it throws before the app even renders. `crypto.getRandomValues()`, by contrast, is
 * available in every context, so we fall back to building the UUID ourselves from it. (Web Crypto's
 * `subtle` is likewise secure-context-only, but nothing here uses it — session auth is a bearer token,
 * not a browser-side hash.)
 */

export function uuid(): string {
  const c = globalThis.crypto;
  if (c && typeof c.randomUUID === 'function') return c.randomUUID();

  const bytes = new Uint8Array(16);
  if (c && typeof c.getRandomValues === 'function') {
    c.getRandomValues(bytes);
  } else {
    // Last-ditch: no Web Crypto at all. Not cryptographically strong, but layout ids only need to be
    // collision-free, and this path should never run in a real browser.
    for (let i = 0; i < 16; i++) bytes[i] = Math.floor(Math.random() * 256);
  }

  // Set the version (4) and variant (10xx) bits per RFC 4122.
  bytes[6] = (bytes[6]! & 0x0f) | 0x40;
  bytes[8] = (bytes[8]! & 0x3f) | 0x80;

  const hex = Array.from(bytes, (b) => b.toString(16).padStart(2, '0'));
  return `${hex.slice(0, 4).join('')}-${hex.slice(4, 6).join('')}-${hex.slice(6, 8).join('')}-${hex
    .slice(8, 10)
    .join('')}-${hex.slice(10, 16).join('')}`;
}
