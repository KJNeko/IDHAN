/**
 * Credential lifecycle for the WebUI.
 *
 * Login is deliberately just "provide a key". Because the server accepts an API key and a session key
 * interchangeably, either one is a valid login. When the key provided is a permanent API key we make a
 * best-effort upgrade to a session key so the permanent key is never persisted in the browser; if that
 * upgrade does not apply (the key is already a session key, or auth is disabled) we simply use the key
 * as given. The stored credential is therefore always usable and, whenever possible, a revocable and
 * expiring session key rather than the permanent one.
 */

import { ApiError, api, getKey, setKey } from '../api/client';

const STORAGE_KEY = 'idhan.session_key';
const KEY_PATTERN = /^[0-9a-f]{64}$/;

export interface AuthedSession {
  key: string;
  /** Unix seconds; null when unknown (e.g. an API key used directly, or a restored key). */
  expiresAt: number | null;
}

/** Normalises user input; returns null if it is not a 64-char hex key. */
function normalise(input: string): string | null {
  const key = input.trim().toLowerCase();
  return KEY_PATTERN.test(key) ? key : null;
}

function persist(key: string): void {
  setKey(key);
  try {
    localStorage.setItem(STORAGE_KEY, key);
  } catch {
    // Private-mode / disabled storage: the in-memory key still works for this tab.
  }
}

function clear(): void {
  setKey(null);
  try {
    localStorage.removeItem(STORAGE_KEY);
  } catch {
    // ignore
  }
}

export class InvalidKeyError extends Error {
  constructor() {
    super('That key was not accepted by the server.');
    this.name = 'InvalidKeyError';
  }
}

/**
 * Log in with a key (API or session). Throws InvalidKeyError if the server rejects it, or an ApiError
 * for transport/other failures. On success the credential is committed to the client and persisted.
 */
export async function login(input: string): Promise<AuthedSession> {
  const provided = normalise(input);
  if (!provided) throw new InvalidKeyError();

  let key = provided;
  let expiresAt: number | null = null;

  // Best-effort upgrade: succeeds only for a permanent API key. A session key (or auth-disabled)
  // either 401s or hands back a placeholder — in every case we fall back to what still works.
  try {
    const grant = await api.auth.createSession(provided);
    if (grant.session_key && KEY_PATTERN.test(grant.session_key)) {
      key = grant.session_key;
      expiresAt = grant.expires_at || null;
    }
  } catch (error) {
    if (!(error instanceof ApiError)) throw error;
    // 401 here just means "not a permanent key" — the provided key may still authenticate directly.
  }

  // Verify the credential we settled on actually authenticates. This is the real gate and works for
  // both key kinds; it also catches a genuinely invalid key that could not be upgraded.
  try {
    await api.auth.checkSession(key);
  } catch (error) {
    clear();
    if (error instanceof ApiError && (error.status === 401 || error.status === 400)) {
      throw new InvalidKeyError();
    }
    throw error;
  }

  persist(key);
  return { key, expiresAt };
}

/**
 * Restore a persisted credential on boot. Returns the session if the stored key still authenticates,
 * or null (clearing the stored key) if there is none or it has expired/been revoked.
 */
export async function restore(): Promise<AuthedSession | null> {
  let stored: string | null = null;
  try {
    stored = localStorage.getItem(STORAGE_KEY);
  } catch {
    stored = null;
  }
  if (!stored || !KEY_PATTERN.test(stored)) {
    clear();
    return null;
  }

  setKey(stored);
  try {
    await api.auth.checkSession(stored);
    return { key: stored, expiresAt: null };
  } catch (error) {
    // A definite rejection (expired/revoked) clears the key. A transport failure (server down) leaves
    // the key set — a reload once the server returns will authenticate — but still reports
    // unauthenticated for now so the login screen shows rather than a spinner.
    if (error instanceof ApiError) clear();
    return null;
  }
}

/** Revoke the current session key (no-op for a permanent key) and forget the credential. */
export async function logout(): Promise<void> {
  const key = getKey();
  if (key) {
    try {
      await api.auth.deleteSession(key);
    } catch {
      // Revocation is best-effort; the local credential is dropped regardless.
    }
  }
  clear();
}
