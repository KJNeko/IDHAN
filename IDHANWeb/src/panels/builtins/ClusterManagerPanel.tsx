/**
 * Cluster Manager: the on-disk storage clusters files live in (GET /clusters/list). Covers the full
 * lifecycle: add a path, toggle read-only, rename, cap size, rescan, and remove. Modify is a PATCH,
 * so the CORS allow-list must permit PATCH for this to be reachable cross-origin.
 */

import { useCallback, useEffect, useState } from 'react';
import type { PanelProps } from '../../host/types';
import { formatBytes } from './RecordInfoView';

export interface ScanParams {
  scan_mime: boolean;
  rescan_mime: boolean;
  scan_metadata: boolean;
  rescan_metadata: boolean;
  verify_hash: boolean;
  adopt_orphans: boolean;
  fix_extensions: boolean;
  stop_on_fail: boolean;
  remove_missing_files: boolean;
  readonly: boolean;
}

export const DEFAULT_SCAN_PARAMS: ScanParams = {
  scan_mime: true,
  rescan_mime: false,
  scan_metadata: true,
  rescan_metadata: false,
  verify_hash: false,
  adopt_orphans: false,
  fix_extensions: false,
  stop_on_fail: false,
  remove_missing_files: false,
  readonly: false,
};

export function fastScanPreset(params: ScanParams): ScanParams {
    return {...params, scan_mime: true, scan_metadata: false, adopt_orphans: true};
}

export function sanitizeScanParams(params: ScanParams, clusterReadonly: boolean): ScanParams {
  const readOnly = clusterReadonly || params.readonly;
  if (!readOnly) return params;
    return {...params, fix_extensions: false, remove_missing_files: false};
}

// Encode as ?scan_mime=true&... since Drogon's fromString<bool> reads "true"/"false".
export function buildScanQuery(params: ScanParams): string {
  const qs = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    qs.set(key, value ? 'true' : 'false');
  }
  return qs.toString();
}

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
    const [newName, setNewName] = useState('');
  const [newPath, setNewPath] = useState('');
  const [newReadonly, setNewReadonly] = useState(true);
  const [scanTarget, setScanTarget] = useState<Cluster | null>(null);
  const [scanParams, setScanParams] = useState<ScanParams>(DEFAULT_SCAN_PARAMS);

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

  useEffect(() => {
    if (!scanTarget) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setScanTarget(null);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [scanTarget]);

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
      const name = newName.trim();
      if (path.length === 0 || name.length === 0) return;
    setBusy(true);
    try {
      const res = await host.http.fetch('/clusters/add', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({name, path, readonly: newReadonly}),
      });
      if (res.status === 409) {
          host.ui.toast('A cluster with that name or path already exists.', {kind: 'error'});
      } else if (!res.ok) {
        const reason = (await res.text()).slice(0, 200);
        throw new Error(reason || `add → ${res.status}`);
      } else {
          host.ui.toast(`Added cluster "${name}" at ${path}.`, {kind: 'success'});
          setNewName('');
        setNewPath('');
        await refresh();
      }
    } catch (err) {
      host.ui.toast(`Add failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  function openScan(c: Cluster) {
    setScanParams(DEFAULT_SCAN_PARAMS);
    setScanTarget(c);
  }

  function closeScan() {
    setScanTarget(null);
  }

  async function runScan(c: Cluster, params: ScanParams) {
    setBusy(true);
    try {
      const qs = buildScanQuery(sanitizeScanParams(params, c.readonly));
      const res = await host.http.fetch(`/clusters/${c.cluster_id}/scan?${qs}`, { method: 'POST' });
      if (!res.ok) throw new Error(`scan → ${res.status}`);
      host.ui.toast(`Scan started for "${c.name}".`, { kind: 'info' });
    } catch (err) {
      host.ui.toast(`Scan failed: ${err instanceof Error ? err.message : String(err)}`, { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

    // One-click hash-only scan, no modal.
  function fastScan(c: Cluster) {
    setScanTarget(null);
    void runScan(c, fastScanPreset(DEFAULT_SCAN_PARAMS));
  }

  async function startScan() {
    const c = scanTarget;
    if (!c) return;
    setScanTarget(null);
    await runScan(c, scanParams);
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

  const scanCheck = (field: keyof ScanParams, label: string, disabled = false) => (
    <label
      className={`log-check${disabled ? ' disabled' : ''}`}
      title={disabled ? 'Unavailable while the scan runs read-only' : undefined}
    >
      <input
        type="checkbox"
        checked={disabled ? false : scanParams[field]}
        disabled={busy || disabled}
        onChange={(e) => setScanParams((p) => ({ ...p, [field]: e.target.checked }))}
      />
      {label}
    </label>
  );

  // The scan is effectively read-only if the cluster is, or the user forced it.
  const effectiveReadonly = scanTarget ? scanTarget.readonly || scanParams.readonly : false;

  return (
    <div className="panel-body cluster-manager">
      <div className="cluster-add">
          <input
              className="search-input"
              value={newName}
              placeholder="Cluster name…"
              disabled={busy}
              onChange={(e) => setNewName(e.target.value)}
              onKeyDown={(e) => {
                  if (e.key === 'Enter') void add();
              }}
              spellCheck={false}
              autoComplete="off"
          />
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
          <button
              type="button"
              className="toolbar-button"
              disabled={busy || newPath.trim().length === 0 || newName.trim().length === 0}
              onClick={() => void add()}
          >
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
                  <button type="button" className="toolbar-button" disabled={busy} onClick={() => openScan(c)}>
                    Scan
                  </button>
                  <button
                    type="button"
                    className="toolbar-button"
                    disabled={busy}
                    title="Hash only, skips mime and metadata"
                    onClick={() => fastScan(c)}
                  >
                    Fast scan
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

      {scanTarget && (
        <div className="scan-modal-overlay" onClick={closeScan}>
          <div className="scan-modal" onClick={(e) => e.stopPropagation()}>
            <div className="scan-modal-title">Scan cluster “{scanTarget.name}”</div>
            {scanTarget.readonly && (
                <div className="muted">This cluster is read-only, so file-modifying options are disabled.</div>
            )}
            <button
              type="button"
              className="toolbar-button scan-modal-preset"
              disabled={busy}
              title="Hash only, skips mime and metadata"
              onClick={() => {
                if (scanTarget) fastScan(scanTarget);
              }}
            >
              Fast scan (hash only)
            </button>
            <div className="scan-modal-grid">
              {scanCheck('scan_mime', 'Scan mime')}
              {scanCheck('rescan_mime', 'Rescan mime')}
              {scanCheck('scan_metadata', 'Scan metadata')}
              {scanCheck('rescan_metadata', 'Rescan metadata')}
              {scanCheck('verify_hash', 'Verify hash')}
              {scanCheck('adopt_orphans', 'Adopt orphans')}
              {scanCheck('fix_extensions', 'Fix extensions', effectiveReadonly)}
              {scanCheck('stop_on_fail', 'Stop on fail')}
            </div>
            <div className="scan-modal-advanced">
              <span className="muted">Advanced</span>
              <div className="scan-modal-grid">
                  {!scanTarget.readonly && scanCheck('remove_missing_files', 'Remove missing files')}
                {!scanTarget.readonly && scanCheck('readonly', 'Force read-only')}
              </div>
            </div>
            <div className="scan-modal-actions">
              <button type="button" className="toolbar-button" disabled={busy} onClick={closeScan}>
                Cancel
              </button>
              <button type="button" className="toolbar-button" disabled={busy} onClick={() => void startScan()}>
                Start scan
              </button>
            </div>
          </div>
        </div>
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
