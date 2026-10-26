/**
 * Import: drag-and-drop or pick files to import into IDHAN. Each file is POSTed to /file/import as raw
 * bytes, not as multipart; the endpoint sniffs the mime from the content. Uploads run through a small
 * concurrency pool so a big drop does not open hundreds of sockets at once.
 * Successfully imported record ids are pushed into the shared selection so other panels can show them.
 */

import {useCallback, useEffect, useRef, useState, type DragEvent} from 'react';
import {api} from '../../api/client';
import type {DownloadSessionInfo, DownloadSessionUrlNode} from '../../api/types';
import type { PanelProps, RecordId } from '../../host/types';
import { RecordInfoView, type RecordInfo } from './RecordInfoView';
import {useRecordMenu} from './recordActions';

const MAX_CONCURRENT = 3;

/** Mirrors ImportStatus in IDHAN/include/codes/ImportCodes.hpp. */
const ImportStatus = {
  Success: 1,
  Exists: 2,
  Deleted: 3,
  Failed: 4,
} as const;

type ItemState = 'pending' | 'uploading' | 'done' | 'skipped' | 'error';

interface Item {
  id: number; // local list key
  name: string;
  size: number;
  state: ItemState;
  /** Short text on the status line, e.g. "#412" or "already present". */
  detail?: string;
  /** Full server message for a failure or skip, wrapped on its own line under the name. */
  reason?: string;
  recordId?: RecordId;
    /** Metadata fetched after a successful import, shown inline. */
  info?: RecordInfo;
}

/**
 * The two response shapes overload `status`: a 2xx carries an ImportStatus, while the error helpers
 * (createBadRequest and friends) put the HTTP code there alongside the message in `error`. Only read
 * `status` as an ImportStatus when the response was ok.
 */
interface ImportResponse {
  record_id?: number;
  status?: number;
  /** Set by the error helpers on any 4xx/5xx. */
  error?: string;
  /** Set on a 200 that still failed, currently only an unidentifiable mime. */
  reason?: string;
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

export function parseNewlineUrlList(value: string): string[] {
    return value
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => line.length > 0);
}

function ImportPanel({ host }: PanelProps) {
    const [sessions, setSessions] = useState<DownloadSessionInfo[]>([]);
    const [activeSessionId, setActiveSessionId] = useState<number | null>(null);
    const [newSessionName, setNewSessionName] = useState('');
    const [downloadUrl, setDownloadUrl] = useState('');
    const [urlJobs, setUrlJobs] = useState<DownloadSessionUrlNode[]>([]);
    const [pollGeneration, setPollGeneration] = useState(0);
    const [sessionBusy, setSessionBusy] = useState(false);
    const [sessionError, setSessionError] = useState<string | null>(null);
  const [items, setItems] = useState<Item[]>([]);
  const [dragOver, setDragOver] = useState(false);
  const [forceImport, setForceImport] = useState(false);
  const [busy, setBusy] = useState(false);
  const fileInput = useRef<HTMLInputElement>(null);
  const nextId = useRef(0);

    const {openRecordMenu, recordMenu} = useRecordMenu(host);

    const loadSessionRecords = useCallback(async (session: DownloadSessionInfo) => {
        const data = await api.downloadSessions.records(session.id);
        host.results.set({
            ids: new Int32Array(data.record_ids),
            queryMs: 0,
            query: [`download session: ${session.name}`],
        });
        host.selection.set([]);
    }, [host]);

    const refreshSessions = useCallback(async () => {
        const listed = await api.downloadSessions.list();
        setSessions(listed);
        setActiveSessionId((current) => current !== null && listed.some((session) => session.id === current) ? current : (listed[0]?.id ?? null));
    }, []);

    useEffect(() => {
        void refreshSessions().catch((error) => {
            setSessionError(error instanceof Error ? error.message : String(error));
        });
    }, [refreshSessions]);

    const activeSession = sessions.find((session) => session.id === activeSessionId) ?? null;

    useEffect(() => {
        if (!activeSession) {
            setUrlJobs([]);
            return;
        }

        const controller = new AbortController();
        let timeout: ReturnType<typeof setTimeout> | undefined;
        let completed = new Set<number>();

        const poll = async () => {
            try {
                const jobs = await api.downloadSessions.urls(activeSession.id, controller.signal);
                if (controller.signal.aborted) return;

                setUrlJobs(jobs);
                const nextCompleted = new Set(jobs.filter((job) => job.state === 'completed').map((job) => job.id));
                const gainedCompletion = [...nextCompleted].some((id) => !completed.has(id));
                completed = nextCompleted;

                if (gainedCompletion) await loadSessionRecords(activeSession);
                if (jobs.some((job) => job.state === 'pending' || job.state === 'processing')) {
                    timeout = setTimeout(() => void poll(), 1500);
                }
            } catch (error) {
                if (!controller.signal.aborted) setSessionError(error instanceof Error ? error.message : String(error));
            }
        };

        void poll();
        return () => {
            controller.abort();
            if (timeout !== undefined) clearTimeout(timeout);
        };
    }, [activeSession?.id, activeSession?.name, loadSessionRecords, pollGeneration]);

    const chooseSession = useCallback(async (id: number) => {
        const session = sessions.find((entry) => entry.id === id);
        if (!session) return;
        setActiveSessionId(id);
        setSessionError(null);
        try {
            await loadSessionRecords(session);
        } catch (error) {
            setSessionError(error instanceof Error ? error.message : String(error));
        }
    }, [loadSessionRecords, sessions]);

    const createSession = useCallback(async () => {
        const name = newSessionName.trim();
        if (!name) return;
        setSessionBusy(true);
        setSessionError(null);
        try {
            const created = await api.downloadSessions.create(name);
            setSessions((previous) => [created, ...previous]);
            setActiveSessionId(created.id);
            setNewSessionName('');
            await loadSessionRecords(created);
        } catch (error) {
            setSessionError(error instanceof Error ? error.message : String(error));
        } finally {
            setSessionBusy(false);
        }
    }, [loadSessionRecords, newSessionName]);

    const queueUrl = useCallback(async () => {
        if (!activeSession) return;

        const urls = parseNewlineUrlList(downloadUrl);
        if (urls.length === 0) return;

        setSessionBusy(true);
        setSessionError(null);
        const failedUrls: string[] = [];
        const errors: string[] = [];
        try {
            for (const url of urls) {
                try {
                    const queued = await api.downloadSessions.submitUrl(activeSession.id, url);
                    setUrlJobs((previous) => [{
                        id: queued.id,
                        parent_id: null,
                        url: queued.url,
                        state: 'pending',
                        created_at: Math.floor(Date.now() / 1000),
                        finished_at: null,
                        error: null,
                        note: null,
                        record_id: null,
                        children: [],
                    }, ...previous]);
                } catch (error) {
                    failedUrls.push(url);
                    errors.push(`${url}: ${error instanceof Error ? error.message : String(error)}`);
                }
            }

            setPollGeneration((value) => value + 1);
            try {
                await refreshSessions();
                await loadSessionRecords(activeSession);
            } catch (error) {
                errors.push(`Unable to refresh the download session: ${error instanceof Error ? error.message : String(error)}`);
            }

            if (failedUrls.length > 0) {
                setDownloadUrl(failedUrls.join('\n'));
                setSessionError(`Failed to queue ${failedUrls.length} URL${failedUrls.length === 1 ? '' : 's'}: ${errors.join('; ')}`);
                return;
            }

            setDownloadUrl('');
            host.ui.toast(`Queued ${urls.length} URL${urls.length === 1 ? '' : 's'} in ${activeSession.name}`, {kind: 'success'});
            if (errors.length > 0) setSessionError(errors.join('; '));
        } finally {
            setSessionBusy(false);
        }
    }, [activeSession, downloadUrl, host, loadSessionRecords, refreshSessions]);

    const retryUrl = useCallback(async (jobId: number) => {
        if (!activeSession) return;
        setSessionError(null);
        try {
            const retried = await api.downloadSessions.retryUrl(activeSession.id, jobId);
            setUrlJobs((previous) => previous.map((job) => (job.id === jobId ? retried : job)));
            setPollGeneration((value) => value + 1);
        } catch (error) {
            setSessionError(error instanceof Error ? error.message : String(error));
        }
    }, [activeSession]);

    const destroySession = useCallback(async () => {
        if (!activeSession) return;
        setSessionBusy(true);
        setSessionError(null);
        try {
            await api.downloadSessions.destroy(activeSession.id);
            setSessions((previous) => previous.filter((session) => session.id !== activeSession.id));
            setActiveSessionId(null);
            host.results.set({ids: new Int32Array(), queryMs: 0, query: []});
            host.selection.set([]);
        } catch (error) {
            setSessionError(error instanceof Error ? error.message : String(error));
        } finally {
            setSessionBusy(false);
        }
    }, [activeSession, host]);

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

        // the name is what tells the server a zip is a comic book zip; nothing in the bytes does
        const queryFor = (file: File) => {
            const params = new URLSearchParams({filename: file.name});
            if (forceImport) params.set('force_import', 'true');
            return `?${params.toString()}`;
        };

      await pool(
        files.map((file, i) => ({ file, item: queued[i]! })),
        MAX_CONCURRENT,
        async ({ file, item }) => {
          patch(item.id, { state: 'uploading' });
          try {
              const res = await host.http.fetch(`/file/import${queryFor(file)}`, {
              method: 'POST',
              headers: { 'Content-Type': file.type || 'application/octet-stream' },
              body: file,
            });
            const data = (await res.json().catch(() => ({}))) as ImportResponse;

            if (!res.ok) {
              patch(item.id, { state: 'error', detail: 'failed', reason: data.error ?? `HTTP ${res.status}` });
              return;
            }

            const recordId = typeof data.record_id === 'number' ? data.record_id : undefined;

            if (data.status === ImportStatus.Failed) {
              patch(item.id, { state: 'error', detail: 'failed', reason: data.reason ?? 'not imported' });
              return;
            }
            if (recordId === undefined) {
              patch(item.id, { state: 'error', detail: 'failed', reason: data.reason ?? data.error ?? 'not imported' });
              return;
            }

            // Exists and Deleted both carry a record id but stored nothing, so neither is an import.
            if (data.status === ImportStatus.Exists || data.status === ImportStatus.Deleted) {
              patch(item.id, {
                state: 'skipped',
                detail: `#${recordId}`,
                reason: data.status === ImportStatus.Exists ? 'already present' : 'previously deleted',
                recordId,
              });
            } else {
              imported.push(recordId);
              patch(item.id, { state: 'done', detail: `#${recordId}`, recordId });
            }

            // Best-effort: a failure just leaves the row without the detail block.
            try {
              const infoRes = await host.http.fetch(`/records/${recordId}/info`);
              if (infoRes.ok) patch(item.id, { info: (await infoRes.json()) as RecordInfo });
            } catch {
              /* ignore */
            }
          } catch (error) {
            patch(item.id, {
              state: 'error',
              detail: 'failed',
              reason: error instanceof Error ? error.message : String(error),
            });
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
  const skipped = items.filter((i) => i.state === 'skipped').length;
  const failed = items.filter((i) => i.state === 'error').length;

  return (
    <div className="panel-body import-panel">
        <section className="download-session-panel">
            <h3>URL download session</h3>
            <div className="download-session-row">
                <select
                    value={activeSessionId ?? ''}
                    disabled={sessionBusy || sessions.length === 0}
                    onChange={(event) => void chooseSession(Number(event.target.value))}
                >
                    {sessions.length === 0 && <option value="">No download sessions</option>}
                    {sessions.map((session) => <option key={session.id} value={session.id}>{session.name}</option>)}
                </select>
                <button type="button" disabled={!activeSession || sessionBusy}
                        onClick={() => void destroySession()}>Delete
                </button>
            </div>
            <div className="download-session-row">
                <input
                    value={newSessionName}
                    onChange={(event) => setNewSessionName(event.target.value)}
                    placeholder="New session name"
                    disabled={sessionBusy}
                    onKeyDown={(event) => {
                        if (event.key === 'Enter') void createSession();
                    }}
                />
                <button type="button" disabled={sessionBusy || !newSessionName.trim()}
                        onClick={() => void createSession()}>New session
                </button>
            </div>
            <div className="download-session-row">
				<textarea
                    value={downloadUrl}
                    onChange={(event) => setDownloadUrl(event.target.value)}
                    placeholder="Paste one URL per line"
                    disabled={sessionBusy || !activeSession}
                />
                <button type="button"
                        disabled={sessionBusy || !activeSession || parseNewlineUrlList(downloadUrl).length === 0}
                        onClick={() => void queueUrl()}>Queue URL(s)
                </button>
            </div>
            <p className="muted">Paste one URL per line. Selecting a session loads its imported files into the image
                grid.</p>
            {urlJobs.length > 0 && (
                <ul className="download-session-jobs" aria-label="URL download queue">
                    {urlJobs.map((job) => (
                        <li key={job.id} className={`state-${job.state}`}>
                            <div className="download-session-job-head">
                                <span className="grow" title={job.url}>{job.url}</span>
                                <span className="download-session-job-state">{job.state}</span>
                                {(job.state === 'completed' || job.state === 'failed') && (
                                    <button
                                        type="button"
                                        className="download-session-job-retry"
                                        title="Queue this URL again"
                                        onClick={() => void retryUrl(job.id)}
                                    >
                                        Retry
                                    </button>
                                )}
                            </div>
                            {job.error && <p className="download-session-job-error">{job.error}</p>}
                        </li>
                    ))}
                </ul>
            )}
            {sessionError && <p className="error">{sessionError}</p>}
        </section>
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
          {done} imported{skipped > 0 ? `, ${skipped} skipped` : ''}
          {failed > 0 ? `, ${failed} failed` : ''}
            {busy ? ' (working…)' : ''}
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
                  {item.state === 'error' ? `✕ ${item.detail ?? 'failed'}` : (item.detail ?? item.state)}
                </span>
              </div>
              {item.reason && (item.state === 'error' || item.state === 'skipped') && (
                <p className="import-reason">{item.reason}</p>
              )}
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
                      onContextMenu={(event) => openRecordMenu(event, [item.recordId!])}
                    />
                  )}
                </div>
              )}
            </li>
          ))}
        </ul>
      )}
        {recordMenu}
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
