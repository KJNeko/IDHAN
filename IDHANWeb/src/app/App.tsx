import { useEffect, useState } from 'react';
import { ApiError, api } from '../api/client';
import type { VersionInfo } from '../api/types';
import { LoginScreen } from '../auth/LoginScreen';
import { useAuth } from '../auth/AuthProvider';

type VersionState =
  | { status: 'loading' }
  | { status: 'ok'; version: VersionInfo }
  | { status: 'error'; message: string };

/**
 * Authenticated shell. For M1 it just proves the authenticated path works end to end — the key is
 * accepted and the API is reachable — by reading /version and offering logout. The panel/layout UI
 * (M3/M4) replaces the body.
 */
function AppShell() {
  const { logout } = useAuth();
  const [state, setState] = useState<VersionState>({ status: 'loading' });

  useEffect(() => {
    const controller = new AbortController();
    api
      .version(controller.signal)
      .then((version) => setState({ status: 'ok', version }))
      .catch((error: unknown) => {
        if (controller.signal.aborted) return;
        const message = error instanceof ApiError ? error.message : error instanceof Error ? error.message : String(error);
        setState({ status: 'error', message });
      });
    return () => controller.abort();
  }, []);

  return (
    <main className="shell">
      <header className="shell-header">
        <h1>IDHAN</h1>
        <button type="button" className="link-button" onClick={() => void logout()}>
          Sign out
        </button>
      </header>
      {state.status === 'loading' && <p className="muted">Contacting server…</p>}
      {state.status === 'error' && <p className="error">Could not reach the server: {state.message}</p>}
      {state.status === 'ok' && (
        <dl className="version">
          <dt>Server</dt>
          <dd>{state.version.idhan_server_version.string}</dd>
          <dt>API</dt>
          <dd>{state.version.idhan_api_version.string}</dd>
          <dt>Hydrus API</dt>
          <dd>{state.version.hydrus_api_version}</dd>
          <dt>Build</dt>
          <dd>
            {state.version.branch}@{state.version.commit.slice(0, 8)}
          </dd>
        </dl>
      )}
    </main>
  );
}

export function App() {
  const { status } = useAuth();

  if (status === 'restoring') {
    return (
      <main className="shell">
        <p className="muted">Restoring session…</p>
      </main>
    );
  }
  if (status === 'unauthenticated') return <LoginScreen />;
  return <AppShell />;
}
