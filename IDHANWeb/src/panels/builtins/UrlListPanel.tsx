/**
 * URLs — known source URLs for the focused record. Reads /records/{id}/urls (a bare string array) and
 * edits via /urls/add and /urls/remove ({ urls: [...] }). Follows the selection like Record Info: the
 * most recently selected record is the subject.
 */

import { useCallback, useEffect, useState, type FormEvent } from 'react';
import type { PanelProps, RecordId } from '../../host/types';

type State =
  | { status: 'idle' }
  | { status: 'loading' }
  | { status: 'ok'; urls: string[] }
  | { status: 'error'; message: string };

function UrlListPanel({ host }: PanelProps) {
  const [focused, setFocused] = useState<RecordId | null>(() => {
    const sel = host.selection.get();
    return sel.length > 0 ? sel[sel.length - 1]! : null;
  });
  const [state, setState] = useState<State>({ status: 'idle' });
  const [draft, setDraft] = useState('');
  const [busy, setBusy] = useState(false);

  useEffect(() =>
    host.selection.subscribe((ids) => {
      setFocused(ids.length > 0 ? ids[ids.length - 1]! : null);
    }), [host]);

  const load = useCallback(
    async (id: RecordId, signal?: AbortSignal) => {
      setState({ status: 'loading' });
      try {
        const res = await host.http.fetch(`/records/${id}/urls`, { signal });
        if (!res.ok) throw new Error(`/records/${id}/urls → ${res.status}`);
        const urls = (await res.json()) as string[];
        setState({ status: 'ok', urls });
      } catch (error) {
        if ((error as { name?: string }).name === 'AbortError') return;
        setState({ status: 'error', message: error instanceof Error ? error.message : String(error) });
      }
    },
    [host],
  );

  useEffect(() => {
    if (focused === null) {
      setState({ status: 'idle' });
      return;
    }
    const controller = new AbortController();
    void load(focused, controller.signal);
    return () => controller.abort();
  }, [focused, load]);

  async function mutate(path: string, url: string) {
    if (focused === null) return;
    setBusy(true);
    try {
      const res = await host.http.fetch(`/records/${focused}/urls/${path}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ urls: [url] }),
      });
      if (!res.ok) throw new Error(`urls/${path} → ${res.status}`);
      await load(focused);
    } catch (error) {
      host.ui.toast(error instanceof Error ? error.message : 'URL update failed', { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  function onAdd(event: FormEvent) {
    event.preventDefault();
    const url = draft.trim();
    if (!url) return;
    setDraft('');
    void mutate('add', url);
  }

  return (
    <div className="panel-body url-list">
      {focused === null && <p className="muted">Select a record to see its URLs.</p>}

      {focused !== null && (
        <>
          <form className="url-add" onSubmit={onAdd}>
            <input
              type="url"
              placeholder="https://…"
              value={draft}
              onChange={(e) => setDraft(e.target.value)}
              aria-label="Add URL"
            />
            <button type="submit" className="toolbar-button" disabled={busy || draft.trim() === ''}>
              Add
            </button>
          </form>

          {state.status === 'loading' && <p className="muted">Loading…</p>}
          {state.status === 'error' && <p className="error">{state.message}</p>}
          {state.status === 'ok' && state.urls.length === 0 && <p className="muted">No URLs on this record.</p>}
          {state.status === 'ok' && state.urls.length > 0 && (
            <ul className="url-items">
              {state.urls.map((url) => (
                <li key={url} className="url-row">
                  <a href={url} target="_blank" rel="noreferrer noopener" className="url-link grow">
                    {url}
                  </a>
                  <button
                    type="button"
                    className="dropdown-item delete"
                    disabled={busy}
                    aria-label={`Remove ${url}`}
                    onClick={() => void mutate('remove', url)}
                  >
                    ✕
                  </button>
                </li>
              ))}
            </ul>
          )}
        </>
      )}
    </div>
  );
}

export const urlListPanel = {
  type: 'url-list',
  title: 'URLs',
  description: 'Known source URLs for the focused record; add and remove them.',
  component: UrlListPanel,
  configVersion: 1,
} as const;
