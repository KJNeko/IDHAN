/**
 * Notes — free-text notes attached to the focused record. Reads /records/{id}/notes ([{note_id,text}]);
 * add posts text/plain to /add_note, remove deletes /remove_note/{note_id}. Both mutations return the
 * updated note list, so the panel adopts the response directly. Follows the selection like Record Info.
 */

import { useCallback, useEffect, useState, type FormEvent } from 'react';
import type { PanelProps, RecordId } from '../../host/types';

interface Note {
  note_id: number;
  text: string;
}

type State =
  | { status: 'idle' }
  | { status: 'loading' }
  | { status: 'ok'; notes: Note[] }
  | { status: 'error'; message: string };

function NotesPanel({ host }: PanelProps) {
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
        const res = await host.http.fetch(`/records/${id}/notes`, { signal });
        if (!res.ok) throw new Error(`/records/${id}/notes → ${res.status}`);
        setState({ status: 'ok', notes: (await res.json()) as Note[] });
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

  async function add(event: FormEvent) {
    event.preventDefault();
    const text = draft.trim();
    if (text === '' || focused === null) return;
    setBusy(true);
    try {
      const res = await host.http.fetch(`/records/${focused}/add_note`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: text,
      });
      if (!res.ok) throw new Error(`add_note → ${res.status}`);
      setDraft('');
      setState({ status: 'ok', notes: (await res.json()) as Note[] });
    } catch (error) {
      host.ui.toast(error instanceof Error ? error.message : 'Adding the note failed', { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  async function remove(noteId: number) {
    if (focused === null) return;
    setBusy(true);
    try {
      const res = await host.http.fetch(`/records/${focused}/remove_note/${noteId}`, { method: 'DELETE' });
      if (!res.ok) throw new Error(`remove_note → ${res.status}`);
      setState({ status: 'ok', notes: (await res.json()) as Note[] });
    } catch (error) {
      host.ui.toast(error instanceof Error ? error.message : 'Removing the note failed', { kind: 'error' });
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="panel-body notes-panel">
      {focused === null && <p className="muted">Select a record to see its notes.</p>}

      {focused !== null && (
        <>
          <form className="note-add" onSubmit={add}>
            <textarea
              placeholder="Add a note…"
              value={draft}
              rows={2}
              onChange={(e) => setDraft(e.target.value)}
              aria-label="Add note"
            />
            <button type="submit" className="toolbar-button" disabled={busy || draft.trim() === ''}>
              Add note
            </button>
          </form>

          {state.status === 'loading' && <p className="muted">Loading…</p>}
          {state.status === 'error' && <p className="error">{state.message}</p>}
          {state.status === 'ok' && state.notes.length === 0 && <p className="muted">No notes on this record.</p>}
          {state.status === 'ok' && state.notes.length > 0 && (
            <ul className="note-items">
              {state.notes.map((note) => (
                <li key={note.note_id} className="note-row">
                  <span className="note-text grow">{note.text}</span>
                  <button
                    type="button"
                    className="dropdown-item delete"
                    disabled={busy}
                    aria-label="Remove note"
                    onClick={() => void remove(note.note_id)}
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

export const notesPanel = {
  type: 'notes',
  title: 'Notes',
  description: 'Free-text notes attached to the focused record.',
  component: NotesPanel,
  configVersion: 1,
} as const;
