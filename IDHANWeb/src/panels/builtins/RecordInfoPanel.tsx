import { useEffect, useState } from 'react';
import type { PanelProps, RecordId } from '../../host/types';
import {readApiError} from '../../api/client';
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
    const [attempt, setAttempt] = useState(0);

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
      const controller = new AbortController();
    setState({ status: 'loading' });
    host.http
        .fetch(`/records/${focused}/info`, {signal: controller.signal})
      .then(async (res) => {
          if (!res.ok) throw await readApiError(res, `GET /records/${focused}/info`);
          const info = await res.json() as Info | null;
          if (info === null || typeof info !== 'object' || info.record_id !== focused) {
              throw new Error('The server returned invalid record details.');
          }
          return info;
      })
      .then((info) => {
        if (!cancelled) setState({ status: 'ok', info });
      })
      .catch((error: unknown) => {
        if (!cancelled) setState({ status: 'error', message: error instanceof Error ? error.message : String(error) });
      });
    return () => {
      cancelled = true;
        controller.abort();
    };
  }, [host, focused, attempt]);

  return (
    <div className="panel-body record-info">
      {selectionCount > 1 && (
          <p className="record-info-selection muted"
             title="Details for the last selected record">{selectionCount.toLocaleString()} selected · #{focused}</p>
      )}
		{state.status === 'idle' && <p className="muted">No record selected.</p>}
        {state.status === 'loading' && <p className="muted" role="status">Loading…</p>}
        {state.status === 'error' && <div className="record-info-error">
            <p className="error" role="alert">{state.message}</p>
            <button type="button" className="toolbar-button" onClick={() => setAttempt(current => current + 1)}>Retry
            </button>
        </div>}
        {state.status === 'ok' && <RecordInfoView key={state.info.record_id} info={state.info}/>}
    </div>
  );
}

export const recordInfoPanel = {
  type: 'record-info',
  title: 'Record Info',
	description: 'File details.',
  component: RecordInfoPanel,
  configVersion: 1,
} as const;
