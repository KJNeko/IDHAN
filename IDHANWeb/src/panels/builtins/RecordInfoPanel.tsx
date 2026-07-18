/**
 * Record Info — details for the focused record (the most recently selected). Uses /records/{id}/info,
 * which carries the basic fields plus whatever file-specific metadata a module produced (dimensions,
 * duration, …). Known fields are formatted; anything else scalar is shown generically so new metadata
 * surfaces without a code change here.
 */

import { useEffect, useState } from 'react';
import type { PanelProps, RecordId } from '../../host/types';

type Info = Record<string, unknown> & { record_id: RecordId };

type State =
  | { status: 'idle' }
  | { status: 'loading' }
  | { status: 'ok'; info: Info }
  | { status: 'error'; message: string };

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value.toFixed(value < 10 ? 1 : 0)} ${units[unit]}`;
}

/** Fields we render explicitly, in order; everything else scalar is appended generically. */
const KNOWN = new Set(['record_id', 'hashes', 'size', 'mime', 'extension', 'width', 'height', 'duration']);

function Row({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <>
      <dt>{label}</dt>
      <dd>{children}</dd>
    </>
  );
}

function RecordInfoPanel({ host }: PanelProps) {
  const [focused, setFocused] = useState<RecordId | null>(() => {
    const sel = host.selection.get();
    return sel.length > 0 ? sel[sel.length - 1]! : null;
  });
  const [selectionCount, setSelectionCount] = useState<number>(() => host.selection.get().length);
  const [state, setState] = useState<State>({ status: 'idle' });

  useEffect(() =>
    host.selection.subscribe((ids) => {
      setSelectionCount(ids.length);
      setFocused(ids.length > 0 ? ids[ids.length - 1]! : null);
    }), [host]);

  useEffect(() => {
    if (focused === null) {
      setState({ status: 'idle' });
      return;
    }
    let cancelled = false;
    setState({ status: 'loading' });
    host.http
      .fetch(`/records/${focused}/info`)
      .then(async (res) => {
        if (!res.ok) throw new Error(`/records/${focused}/info → ${res.status}`);
        return (await res.json()) as Info;
      })
      .then((info) => {
        if (!cancelled) setState({ status: 'ok', info });
      })
      .catch((error: unknown) => {
        if (!cancelled) setState({ status: 'error', message: error instanceof Error ? error.message : String(error) });
      });
    return () => {
      cancelled = true;
    };
  }, [host, focused]);

  return (
    <div className="panel-body record-info">
      {selectionCount > 1 && (
        <p className="muted">{selectionCount.toLocaleString()} records selected; showing the last.</p>
      )}
      {state.status === 'idle' && <p className="muted">Select a record to see its details.</p>}
      {state.status === 'loading' && <p className="muted">Loading…</p>}
      {state.status === 'error' && <p className="error">{state.message}</p>}
      {state.status === 'ok' && <RecordInfoBody info={state.info} />}
    </div>
  );
}

function RecordInfoBody({ info }: { info: Info }) {
  const sha256 = (info.hashes as { sha256?: string } | undefined)?.sha256;
  const size = typeof info.size === 'number' ? info.size : undefined;
  const width = typeof info.width === 'number' ? info.width : undefined;
  const height = typeof info.height === 'number' ? info.height : undefined;
  const duration = typeof info.duration === 'number' ? info.duration : undefined;

  const extras = Object.entries(info).filter(
    ([key, value]) => !KNOWN.has(key) && (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean'),
  );

  return (
    <dl className="info-list">
      <Row label="Record">#{info.record_id}</Row>
      {typeof info.mime === 'string' && <Row label="Type">{info.mime}</Row>}
      {typeof info.extension === 'string' && <Row label="Extension">.{info.extension}</Row>}
      {size !== undefined && (
        <Row label="Size">
          {formatBytes(size)} <span className="muted">({size.toLocaleString()} B)</span>
        </Row>
      )}
      {width !== undefined && height !== undefined && <Row label="Dimensions">{width} × {height}</Row>}
      {duration !== undefined && <Row label="Duration">{duration.toFixed(2)} s</Row>}
      {sha256 && (
        <Row label="SHA-256">
          <span className="mono">{sha256}</span>
        </Row>
      )}
      {extras.map(([key, value]) => (
        <Row key={key} label={key}>
          {String(value)}
        </Row>
      ))}
    </dl>
  );
}

export const recordInfoPanel = {
  type: 'record-info',
  title: 'Record Info',
  description: 'Details for the focused record: hash, type, size, dimensions, and more.',
  component: RecordInfoPanel,
  configVersion: 1,
} as const;
