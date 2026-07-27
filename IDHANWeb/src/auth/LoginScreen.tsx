/**
 * Single-field login: paste an IDHAN API key and go. The key is validated against the server and,
 * once accepted, used directly on every request.
 */

import { useState, type FormEvent } from 'react';
import { ApiError } from '../api/client';
import { InvalidKeyError } from './session';
import { useAuth } from './AuthProvider';

export function LoginScreen() {
  const { login } = useAuth();
  const [key, setKey] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function onSubmit(event: FormEvent) {
    event.preventDefault();
    if (busy) return;
    setBusy(true);
    setError(null);
    try {
      await login(key);
    } catch (err) {
      if (err instanceof InvalidKeyError) setError(err.message);
      else if (err instanceof ApiError) setError(`Server error (${err.status}). Please try again.`);
      else setError('Could not reach the server. Is it running?');
      setBusy(false);
    }
  }

  return (
    <main className="login">
      <form className="login-card" onSubmit={onSubmit}>
        <h1>IDHAN</h1>
        <p className="muted">Enter your access key to continue.</p>
        <input
          type="password"
          className="login-input"
          placeholder="64-character key"
          autoComplete="off"
          autoFocus
          spellCheck={false}
          value={key}
          onChange={(e) => setKey(e.target.value)}
          disabled={busy}
        />
        {error && <p className="error">{error}</p>}
        <button type="submit" className="login-button" disabled={busy || key.trim().length === 0}>
          {busy ? 'Signing in…' : 'Sign in'}
        </button>
      </form>
    </main>
  );
}
