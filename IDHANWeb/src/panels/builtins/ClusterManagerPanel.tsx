/**
 * Cluster Manager — the on-disk storage clusters files live in (GET /clusters/list). Supports the full
 * lifecycle: add a path, toggle read-only, rename, cap size, rescan, and remove. The modify call is a
 * PATCH (the CORS allow-list had to gain PATCH for this to be reachable cross-origin).
 */

import { useCallback, useEffect, useState } from 'react';
import type { PanelProps } from '../../host/types';
import { formatBytes } from './RecordInfoView';

interface Cluster {
  cluster_id: number;
  name: string;
  path: string;
  readonly: boolean;
  file_count: number;
  ratio_number: number;
  size: { used: number; limit: number; available: number };
}

function ClusterManagerPanel({ host }: PanelProps) {
  const [clusters, setClusters] = useState<Cluster[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [newPath, setNewPath] = useState('');
  const [newReadonly, setNewReadonly] = useState(true);

  const refresh = useCallback(async () => {
    try {
      const res = await host.http.fetch('/clusters/list');
      if (!res.ok) throw new Error(`/clusters/list → ${res.status}`);
      setClusters((await res.json()) as Cluster[]);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [host]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const patch = useCallback(
    async (id: number, body: unknown, describe: string) => {
      setBusy(true);
      try {
        const res = await host.http.fetch(`/clusters/${id}/modify`, {
          method: 'PATCH',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(body),
        });
        if (res.status === 409) {
          host.ui.toast('That name is already taken by another cluster.', { kind: 'error' });
        } else if (!res.ok) {
          throw new Error(`modify → ${res.status}`);
        } else {
          host.ui.toast(describe, { kind: 'success' });
          await refresh();
        }
      } catch (err) {
        host.ui.toast(`Update failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
      } finally {
        setBusy(false);
      }
    },
    [host, refresh],
  );

  async function add() {
    const path = newPath.trim();
    if (path.length === 0) return;
    setBusy(true);
    try {
      const res = await host.http.fetch('/clusters/add', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path, readonly: newReadonly }),
      });
      if (res.status === 409) {
        host.ui.toast('A cluster with that path already exists.', { kind: 'error' });
      } else if (!res.ok) {
        const reason = (await res.text()).slice(0, 200);
        throw new Error(reason || `add → ${res.status}`);
      } else {
        host.ui.toast(`Added cluster at ${path}.`, { kind: 'success' });
        setNewPath('');
        await refresh();
      }
    } catch (err) {
      host.ui.toast(`Add failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  async function scan(c: Cluster) {
    setBusy(true);
    try {
      const res = await host.http.fetch(`/clusters/${c.cluster_id}/scan`, { method: 'POST' });
      if (!res.ok) throw new Error(`scan → ${res.status}`);
      host.ui.toast(`Scan started for "${c.name}".`, { kind: 'info' });
    } catch (err) {
      host.ui.toast(`Scan failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  async function remove(c: Cluster) {
    if (!window.confirm(`Remove cluster "${c.name}" (${c.path})? The files on disk are left in place.`)) return;
    setBusy(true);
    try {
      const res = await host.http.fetch(`/clusters/${c.cluster_id}/remove`, { method: 'DELETE' });
      if (!res.ok) throw new Error(`remove → ${res.status}`);
      host.ui.toast(`Removed cluster "${c.name}".`, { kind: 'success' });
      await refresh();
    } catch (err) {
      host.ui.toast(`Remove failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  function rename(c: Cluster) {
    const next = window.prompt(`Rename cluster "${c.name}"`, c.name);
    if (next === null) return;
    const trimmed = next.trim();
    if (trimmed.length === 0 || trimmed === c.name) return;
    void patch(c.cluster_id, { name: trimmed }, `Renamed to "${trimmed}".`);
  }

  function setLimit(c: Cluster) {
    const current = c.size.limit === 0 ? '' : String(c.size.limit);
    const next = window.prompt(`Size limit for "${c.name}" in bytes (0 = unlimited)`, current);
    if (next === null) return;
    const bytes = Number(next.trim());
    if (!Number.isFinite(bytes) || bytes < 0) {
      host.ui.toast('Enter a non-negative number of bytes.', { kind: 'error' });
      return;
    }
    void patch(c.cluster_id, { size: { limit: bytes } }, `Set size limit to ${bytes === 0 ? 'unlimited' : formatBytes(bytes)}.`);
  }

  return (
    <div className="panel-body cluster-manager">
      <div className="cluster-add">
        <input
          className="search-input grow"
          value={newPath}
          placeholder="New cluster path (server-side)…"
          disabled={busy}
          onChange={(e) => setNewPath(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') void add();
          }}
          spellCheck={false}
          autoComplete="off"
        />
        <label className="log-check" title="A read-only cluster must already exist on disk.">
          <input type="checkbox" checked={newReadonly} onChange={(e) => setNewReadonly(e.target.checked)} />
          Read-only
        </label>
        <button type="button" className="toolbar-button" disabled={busy || newPath.trim().length === 0} onClick={() => void add()}>
          Add
        </button>
      </div>

      {error && <p className="error">{error}</p>}
      {clusters !== null && clusters.length === 0 && <p className="muted">No storage clusters configured.</p>}
      {clusters !== null && clusters.length > 0 && (
        <ul className="cluster-list">
          {clusters.map((c) => {
            const pct = c.size.limit > 0 ? Math.min(100, (c.size.used / c.size.limit) * 100) : null;
            return (
              <li key={c.cluster_id} className="cluster-card">
                <div className="cluster-head">
                  <span className="cluster-name grow">
                    {c.name} <span className="muted">#{c.cluster_id}</span>
                  </span>
                  {c.readonly && <span className="tag-badge">read-only</span>}
                </div>
                <div className="cluster-path muted" title={c.path}>
                  {c.path}
                </div>
                <div className="cluster-stats muted">
                  {c.file_count.toLocaleString()} files · {formatBytes(c.size.used)}
                  {c.size.limit > 0 ? ` / ${formatBytes(c.size.limit)}` : ' / ∞'} · ratio {c.ratio_number}
                </div>
                {pct !== null && (
                  <div className="cluster-bar">
                    <div className="cluster-bar-fill" style={{ width: `${pct}%` }} />
                  </div>
                )}
                <div className="cluster-actions">
                  <button type="button" className="toolbar-button" disabled={busy} onClick={() => rename(c)}>
                    Rename
                  </button>
                  <button type="button" className="toolbar-button" disabled={busy} onClick={() => setLimit(c)}>
                    Size limit
                  </button>
                  <button
                    type="button"
                    className="toolbar-button"
                    disabled={busy}
                    onClick={() => void patch(c.cluster_id, { readonly: !c.readonly }, c.readonly ? 'Now writable.' : 'Now read-only.')}
                  >
                    {c.readonly ? 'Make writable' : 'Make read-only'}
                  </button>
                  <button type="button" className="toolbar-button" disabled={busy} onClick={() => void scan(c)}>
                    Scan
                  </button>
                  <button type="button" className="toolbar-button danger" disabled={busy} onClick={() => void remove(c)}>
                    Remove
                  </button>
                </div>
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}

export const clusterManagerPanel = {
  type: 'cluster-manager',
  title: 'Cluster Manager',
  description: 'Manage on-disk storage clusters: add, rename, cap, scan, and remove.',
  component: ClusterManagerPanel,
  configVersion: 1,
  singleton: true,
} as const;
