/**
 * React binding over the credential lifecycle in session.ts.
 *
 * On mount it tries to restore a persisted key. Children read the resulting status through useAuth()
 * and gate login-vs-app on it; the actual credential never leaves the client/session layer.
 */

import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react';
import { type AuthedSession, login as doLogin, logout as doLogout, restore } from './session';

type Status = 'restoring' | 'unauthenticated' | 'authenticated';

interface AuthState {
  status: Status;
  session: AuthedSession | null;
  login: (key: string) => Promise<void>;
  logout: () => Promise<void>;
}

const AuthContext = createContext<AuthState | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [status, setStatus] = useState<Status>('restoring');
  const [session, setSession] = useState<AuthedSession | null>(null);

  useEffect(() => {
    let cancelled = false;
    restore()
      .then((restored) => {
        if (cancelled) return;
        setSession(restored);
        setStatus(restored ? 'authenticated' : 'unauthenticated');
      })
      .catch(() => {
        if (cancelled) return;
        setStatus('unauthenticated');
      });
    return () => {
      cancelled = true;
    };
  }, []);

  const login = useCallback(async (key: string) => {
    const next = await doLogin(key);
    setSession(next);
    setStatus('authenticated');
  }, []);

  const logout = useCallback(async () => {
    await doLogout();
    setSession(null);
    setStatus('unauthenticated');
  }, []);

  const value = useMemo<AuthState>(() => ({ status, session, login, logout }), [status, session, login, logout]);

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth must be used within an AuthProvider');
  return ctx;
}
