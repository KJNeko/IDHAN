/**
 * Import — drag-and-drop (or pick) files to import into IDHAN. Each file is POSTed to /file/import as
 * *raw bytes* (the endpoint sniffs the mime from content; it is NOT multipart — that is the easy trap).
 * Uploads run through a small concurrency pool so a big drop doesn't open hundreds of sockets at once.
 * Successfully imported record ids are pushed into the shared selection so other panels can show them.
 */

import { useCallback, useRef, useState, type DragEvent } from 'react';
import type { PanelProps, RecordId } from '../../host/types';
import { RecordInfoView, type RecordInfo } from './RecordInfoView';

const MAX_CONCURRENT = 3;

type ItemState = 'pending' | 'uploading' | 'done' | 'error';

interface Item {
  id: number; // local list key
  name: string;
  size: number;
  state: ItemState;
  detail?: string;
  recordId?: RecordId;
  /** Metadata fetched after a successful import, shown inline (same view as the Record Info panel). */
  info?: RecordInfo;
}

/** Run `worker` over `items` with at most `limit` in flight at once. */
async function pool<T>(items: T[], limit: number, worker: (item: T, index: number) => Promise<void>): Promise<void> {
  let next = 0;
  const runners = Array.from({ length: Math.min(limit, items.length) }, async () => {
    while (next < items.length) {
      const index = next++;
      await worker(items[index]!, index);
    }
  });
  await Promise.all(runners);
}

function ImportPanel({ host }: PanelProps) {
  const [items, setItems] = useState<Item[]>([]);
  const [dragOver, setDragOver] = useState(false);
  const [forceImport, setForceImport] = useState(false);
  const [busy, setBusy] = useState(false);
  const fileInput = useRef<HTMLInputElement>(null);
  const nextId = useRef(0);

  const patch = useCallback((id: number, changes: Partial<Item>) => {
    setItems((prev) => prev.map((item) => (item.id === id ? { ...item, ...changes } : item)));
  }, []);

  const importFiles = useCallback(
    async (files: File[]) => {
      if (files.length === 0) return;
      const queued: Item[] = files.map((file) => ({
        id: nextId.current++,
        name: file.name,
        size: file.size,
        state: 'pending',
      }));
      setItems((prev) => [...queued, ...prev]);
      setBusy(true);

      const imported: RecordId[] = [];
      const query = forceImport ? '?force_import=true' : '';

      await pool(
        files.map((file, i) => ({ file, item: queued[i]! })),
        MAX_CONCURRENT,
        async ({ file, item }) => {
          patch(item.id, { state: 'uploading' });
          try {
            const res = await host.http.fetch(`/file/import${query}`, {
              method: 'POST',
              headers: { 'Content-Type': file.type || 'application/octet-stream' },
              body: file,
            });
            const data = (await res.json().catch(() => ({}))) as {
              record_id?: number;
              reason?: string;
              status?: number;
            };
            if (!res.ok) {
              patch(item.id, { state: 'error', detail: data.reason ?? `HTTP ${res.status}` });
              return;
            }
            if (typeof data.record_id === 'number') {
              imported.push(data.record_id);
              patch(item.id, { state: 'done', detail: `#${data.record_id}`, recordId: data.record_id });
              // Best-effort: show the metadata we got, the same view as the Record Info panel. A failure
              // here just leaves the row without the detail block — the import itself already succeeded.
              try {
                const infoRes = await host.http.fetch(`/records/${data.record_id}/info`);
                if (infoRes.ok) patch(item.id, { info: (await infoRes.json()) as RecordInfo });
              } catch {
                /* ignore */
              }
            } else {
              // e.g. unknown mime without force: the server returns a reason, not a record.
              patch(item.id, { state: 'error', detail: data.reason ?? 'not imported' });
            }
          } catch (error) {
            patch(item.id, { state: 'error', detail: error instanceof Error ? error.message : String(error) });
          }
        },
      );

      setBusy(false);
      if (imported.length > 0) {
        host.selection.set(imported);
        host.ui.toast(`Imported ${imported.length} file${imported.length === 1 ? '' : 's'}`, { kind: 'success' });
      }
    },
    [host, forceImport, patch],
  );

  function onDrop(event: DragEvent) {
    event.preventDefault();
    setDragOver(false);
    void importFiles(Array.from(event.dataTransfer.files));
  }

  const done = items.filter((i) => i.state === 'done').length;
  const failed = items.filter((i) => i.state === 'error').length;

  return (
    <div className="panel-body import-panel">
      <div
        className={`import-drop${dragOver ? ' is-over' : ''}`}
        onDragOver={(e) => {
          e.preventDefault();
          setDragOver(true);
        }}
        onDragLeave={() => setDragOver(false)}
        onDrop={onDrop}
        onClick={() => fileInput.current?.click()}
        role="button"
        tabIndex={0}
      >
        <p>Drop files here, or click to choose.</p>
        <input
          ref={fileInput}
          type="file"
          multiple
          hidden
          onChange={(e) => {
            void importFiles(Array.from(e.target.files ?? []));
            e.target.value = '';
          }}
        />
      </div>

      <label className="import-force">
        <input type="checkbox" checked={forceImport} onChange={(e) => setForceImport(e.target.checked)} />
        Force import files with an unknown type
      </label>

      {items.length > 0 && (
        <p className="muted">
          {done} imported{failed > 0 ? `, ${failed} failed` : ''}
          {busy ? ' — working…' : ''}
        </p>
      )}

      {items.length > 0 && (
        <ul className="import-items">
          {items.map((item) => (
            <li key={item.id} className={`import-row state-${item.state}`}>
              <div className="import-row-head">
                <span className="grow" title={item.name}>
                  {item.name}
                </span>
                <span className="muted import-status">
                  {item.state === 'done' ? item.detail : item.state === 'error' ? `✕ ${item.detail ?? ''}` : item.state}
                </span>
              </div>
              {item.info && (
                <div className="import-meta">
                  <div className="import-meta-info grow">
                    <RecordInfoView info={item.info} />
                  </div>
                  {item.recordId !== undefined && (
                    <img
                      className="import-thumb"
                      src={host.records.thumbnailUrl(item.recordId, 128)}
                      alt={`Thumbnail of #${item.recordId}`}
                      loading="lazy"
                      decoding="async"
                      draggable={false}
                    />
                  )}
                </div>
              )}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

export const importPanel = {
  type: 'import',
  title: 'Import',
  description: 'Drag-and-drop files to import them into IDHAN.',
  component: ImportPanel,
  configVersion: 1,
} as const;
