import { useEffect, useState } from 'react';

interface SemVer {
  major: number;
  minor: number;
  patch: number;
  string: string;
}

interface VersionInfo {
  idhan_server_version: SemVer;
  // An object, unlike hydrus_api_version, which really is a bare number.
  idhan_api_version: SemVer;
  hydrus_api_version: number;
  hydrus_version: number;
  branch: string;
  commit: string;
  build: string;
}

type State =
  | { status: 'loading' }
  | { status: 'ok'; version: VersionInfo }
  | { status: 'error'; message: string };

/**
 * Placeholder shell for the WebUI.
 *
 * It calls /version rather than rendering a static greeting so that a successful load proves the
 * whole path works end to end: the bundle is served, the API prefix reaches the server (through the
 * Vite proxy in dev, same-origin in production), and JSON comes back.
 */
export function App() {
  const [state, setState] = useState<State>({ status: 'loading' });

  useEffect(() => {
    const controller = new AbortController();

    fetch('/version', { signal: controller.signal })
      .then(async (response) => {
        if (!response.ok) throw new Error(`/version returned ${response.status}`);
        setState({ status: 'ok', version: (await response.json()) as VersionInfo });
      })
      .catch((error: unknown) => {
        if (controller.signal.aborted) return;
        setState({ status: 'error', message: error instanceof Error ? error.message : String(error) });
      });

    return () => controller.abort();
  }, []);

  return (
    <main className="shell">
      <h1>IDHAN</h1>
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
