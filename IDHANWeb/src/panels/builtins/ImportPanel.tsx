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
import {ImportStorageNotice, useImportStorage} from './importStorage';
import {captureDroppedFiles, collectImportFiles, describeImportError, type ImportSelection} from './importFiles';
import {runImportPool, uploadImportFile} from './fileImport';

type ItemState = 'pending' | 'uploading' | 'done' | 'skipped' | 'error';

interface Item {
  id: number; // local list key
  name: string;
  size: number;
  state: ItemState;
  detail?: string;
  reason?: string;
  recordId?: RecordId;
  info?: RecordInfo;
}

export function parseNewlineUrlList(value: string): string[] {
    return value
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => line.length > 0);
}

function ImportPanel({ host }: PanelProps) {
    const storage = useImportStorage(host);
    const {canImport, check: checkStorage} = storage;
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
    const folderInput = useRef<HTMLInputElement>(null);
    const importPending = useRef(false);
    const [reading, setReading] = useState(false);
    const [readErrors, setReadErrors] = useState<string[]>([]);
    const [emptySelection, setEmptySelection] = useState(false);
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
                const finished = (job: DownloadSessionUrlNode) => job.state === 'completed' || job.state === 'imported'
                    || job.state === 'imported_previously';
                const nextCompleted = new Set(jobs.filter(finished).map((job) => job.id));
                const gainedCompletion = [...nextCompleted].some((id) => !completed.has(id));
                completed = nextCompleted;

                if (gainedCompletion) await loadSessionRecords(activeSession);
                const unfinished = ['pending', 'downloading', 'processing', 'importing'];
                if (jobs.some((job) => unfinished.includes(job.state))) {
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
        if (!activeSession || !canImport || sessionBusy) return;

        const urls = parseNewlineUrlList(downloadUrl);
        if (urls.length === 0) return;

        setSessionBusy(true);
        setSessionError(null);
        const failedUrls: string[] = [];
        const errors: string[] = [];
        try {
            if (!await checkStorage()) return;
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
                        stage: null,
                        record_id: null,
                        size: null,
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
    }, [activeSession, canImport, checkStorage, downloadUrl, host, loadSessionRecords, refreshSessions, sessionBusy]);

    const retryUrl = useCallback(async (jobId: number) => {
        if (!activeSession || !canImport) return;
        setSessionError(null);
        try {
            if (!await checkStorage()) return;
            const retried = await api.downloadSessions.retryUrl(activeSession.id, jobId);
            setUrlJobs((previous) => previous.map((job) => (job.id === jobId ? retried : job)));
            setPollGeneration((value) => value + 1);
        } catch (error) {
            setSessionError(error instanceof Error ? error.message : String(error));
        }
    }, [activeSession, canImport, checkStorage]);

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
      async (selection: ImportSelection) => {
          if (!canImport || importPending.current) return;
          importPending.current = true;
          setBusy(true);
          setReadErrors([]);
          setEmptySelection(false);
          try {
              if (!await checkStorage()) return;
              setReading(true);
              const collected = await collectImportFiles(selection);
              setReading(false);
              setReadErrors(collected.errors);
              setEmptySelection(collected.files.length === 0 && collected.errors.length === 0);
              const files = collected.files;
              const queued: Item[] = files.map(({file}) => ({
                  id: nextId.current++, name: file.name, size: file.size, state: 'pending',
              }));
              setItems(previous => [...queued, ...previous]);
              const imported: RecordId[] = [];
              await runImportPool(files.map(({file}, index) => ({file, item: queued[index]!})), async ({
                                                                                                           file,
                                                                                                           item
                                                                                                       }) => {
                  patch(item.id, {state: 'uploading'});
          try {
              const result = await uploadImportFile(host, file, forceImport);
              const recordId = result.recordId;
              if (result.outcome === 'imported') imported.push(recordId);
              patch(item.id, {
                  state: result.outcome === 'imported' ? 'done' : 'skipped', detail: `#${recordId}`, recordId,
                  reason: result.outcome === 'exists' ? 'already present' : result.outcome === 'deleted' ? 'previously deleted' : undefined,
              });
            try {
                const response = await host.http.fetch(`/records/${recordId}/info`);
                if (response.ok) patch(item.id, {info: await response.json() as RecordInfo});
            } catch { /* Optional metadata does not change the outcome of the import. */
            }
          } catch (error) {
              patch(item.id, {state: 'error', detail: 'failed', reason: describeImportError(error)});
          }
              });

              if (imported.length > 0) {
                  host.selection.set(imported);
                  host.ui.toast(`Imported ${imported.length} file${imported.length === 1 ? '' : 's'}`, {kind: 'success'});
              }
          } catch (error) {
              setReadErrors(previous => [...previous, `Could not prepare files for import: ${describeImportError(error)}`]);
          } finally {
              importPending.current = false;
              setReading(false);
              setBusy(false);
      }
    },
      [host, forceImport, patch, canImport, checkStorage],
  );

  function onDrop(event: DragEvent) {
    event.preventDefault();
    setDragOver(false);
      if (!canImport || importPending.current) return;
      void importFiles(captureDroppedFiles(event.dataTransfer));
  }

  const done = items.filter((i) => i.state === 'done').length;
  const skipped = items.filter((i) => i.state === 'skipped').length;
  const failed = items.filter((i) => i.state === 'error').length;

  return (
    <div className="panel-body import-panel">
        <ImportStorageNotice storage={storage}/>
        <section className="download-session-panel">
            <div className="download-session-row">
                <select
                    aria-label="Download session"
                    title="Show imported files in the grid"
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
                    disabled={sessionBusy || !activeSession || !canImport}
                />
                <button type="button"
                        disabled={sessionBusy || !activeSession || !canImport || parseNewlineUrlList(downloadUrl).length === 0}
                        onClick={() => void queueUrl()}>Queue URL(s)
                </button>
            </div>
            {urlJobs.length > 0 && (
                <ul className="download-session-jobs" aria-label="URL download queue">
                    {urlJobs.map((job) => (
                        <li key={job.id} className={`state-${job.state}`}>
                            <div className="download-session-job-head">
                                <span className="grow" title={job.url}>{job.url}</span>
                                <span className="download-session-job-state">{job.state}</span>
                                {['completed', 'imported', 'imported_previously', 'failed'].includes(job.state) && (
                                    <button
                                        type="button"
                                        className="download-session-job-retry"
                                        title="Queue this URL again"
                                        disabled={!canImport}
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
          className={`import-drop${dragOver ? ' is-over' : ''}${!canImport || busy ? ' is-disabled' : ''}`}
        onDragOver={(e) => {
          e.preventDefault();
            if (canImport && !busy) setDragOver(true);
        }}
          onKeyDown={event => {
              if (event.key !== 'Enter' && event.key !== ' ') return;
              event.preventDefault();
              if (canImport && !busy) fileInput.current?.click();
          }}
        onDragLeave={() => setDragOver(false)}
        onDrop={onDrop}
          onClick={() => {
              if (canImport && !busy) fileInput.current?.click();
          }}
        role="button"
          aria-disabled={!canImport || busy}
          tabIndex={canImport && !busy ? 0 : -1}
      >
		  <span>Choose files…</span>
        <input
          ref={fileInput}
          type="file"
          multiple
          hidden
          onClick={event => event.stopPropagation()}
          disabled={!canImport || busy}
          onChange={(e) => {
              void importFiles({sources: Array.from(e.target.files ?? [])});
            e.target.value = '';
          }}
        />
      </div>

        <div className="panel-row">
            <button type="button" className="toolbar-button" disabled={!canImport || busy}
                    onClick={() => folderInput.current?.click()}>Choose folder…
            </button>
            <input type="file" multiple hidden aria-label="Import folder"
                   ref={node => {
                       folderInput.current = node;
                       if (node) node.webkitdirectory = true;
                   }}
                   disabled={!canImport || busy}
                   onChange={event => {
                       void importFiles({sources: Array.from(event.target.files ?? [])});
                       event.target.value = '';
                   }}/>
            {reading && <span className="muted" role="status">Reading files and folders…</span>}
        </div>
        {readErrors.length > 0 && <ul className="import-read-errors" role="alert">
            {readErrors.map((error, index) => <li key={index}>{error}</li>)}
        </ul>}
        {emptySelection && <p className="muted" role="status">No files found in the selected folders.</p>}

      <label className="import-force">
          <input type="checkbox" checked={forceImport} disabled={!canImport || busy}
                 onChange={(e) => setForceImport(e.target.checked)}/>
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
                  <p className="import-reason" role={item.state === 'error' ? 'alert' : undefined}>{item.reason}</p>
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
	description: 'Import files and URLs.',
  component: ImportPanel,
  configVersion: 1,
} as const;
