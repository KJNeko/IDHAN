/**
 * Server Status — the panel that absorbs the old static index.html. Reads /version through the host's
 * fetch escape hatch (dogfooding the same surface plugins get) rather than importing the API client.
 */

import { useEffect, useState } from 'react';
import type { PanelProps } from '../../host/types';
import type { VersionInfo } from '../../api/types';

type State =
  | { status: 'loading' }
  | { status: 'ok'; version: VersionInfo }
  | { status: 'error'; message: string };

function ServerStatusPanel({ host }: PanelProps) {
  const [state, setState] = useState<State>({ status: 'loading' });

  useEffect(() => {
    let cancelled = false;
    host.http
      .fetch('/version')
      .then(async (res) => {
        if (!res.ok) throw new Error(`/version → ${res.status}`);
        return (await res.json()) as VersionInfo;
      })
      .then((version) => {
        if (!cancelled) setState({ status: 'ok', version });
      })
      .catch((error: unknown) => {
        if (!cancelled) setState({ status: 'error', message: error instanceof Error ? error.message : String(error) });
      });
    return () => {
      cancelled = true;
    };
  }, [host]);

  return (
    <div className="panel-body">
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
    </div>
  );
}

export const serverStatusPanel = {
  type: 'server-status',
  title: 'Server Status',
  description: 'IDHAN server version and build information.',
  component: ServerStatusPanel,
  singleton: true,
} as const;
