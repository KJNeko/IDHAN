/**
 * Credential lifecycle for the WebUI. Login is just "provide a key": the permanent API key is
 * validated against the server and, if accepted, stored and presented directly on every request.
 * There is no session-key exchange.
 */

import {ApiError, api, setKey} from '../api/client';

const STORAGE_KEY = 'idhan.api_key';
const KEY_PATTERN = /^[0-9a-f]{64}$/;

export interface AuthedSession {
  key: string;
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
 * Log in with an API key. Throws InvalidKeyError if the server rejects it, or an ApiError for
 * transport/other failures. On success the credential is committed to the client and persisted.
 */
export async function login(input: string): Promise<AuthedSession> {
    const key = normalise(input);
    if (!key) throw new InvalidKeyError();

    // Verify the key actually authenticates before committing it.
  try {
      await api.auth.verifyKey(key);
  } catch (error) {
    clear();
    if (error instanceof ApiError && (error.status === 401 || error.status === 400)) {
      throw new InvalidKeyError();
    }
    throw error;
  }

  persist(key);
    return {key};
}

/**
 * Restore a persisted credential on boot. Returns the session if the stored key still authenticates,
 * or null (clearing the stored key) if there is none or it has been rejected.
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
      await api.auth.verifyKey(stored);
      return {key: stored};
  } catch (error) {
    if (error instanceof ApiError) clear();
    return null;
  }
}

/** Forget the current credential. */
export async function logout(): Promise<void> {
  clear();
}
