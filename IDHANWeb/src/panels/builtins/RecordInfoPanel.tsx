/**
 * Record Info — details for the focused record (the most recently selected). Uses /records/{id}/info,
 * which carries the basic fields plus whatever file-specific metadata a module produced (dimensions,
 * duration, …). The presentation lives in the shared RecordInfoView so the Import panel can reuse it.
 */

import { useEffect, useState } from 'react';
import type { PanelProps, RecordId } from '../../host/types';
import { RecordInfoView, type RecordInfo } from './RecordInfoView';

type Info = RecordInfo;

type State =
  | { status: 'idle' }
  | { status: 'loading' }
  | { status: 'ok'; info: Info }
  | { status: 'error'; message: string };

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
      {state.status === 'ok' && <RecordInfoView info={state.info} />}
    </div>
  );
}

export const recordInfoPanel = {
  type: 'record-info',
  title: 'Record Info',
  description: 'Details for the focused record: hash, type, size, dimensions, and more.',
  component: RecordInfoPanel,
  configVersion: 1,
} as const;
