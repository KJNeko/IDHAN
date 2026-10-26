import {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import {api, watchDownloadSessions} from '../../api/client';
import type {
    DownloadSessionError,
    DownloadSessionErrorLog,
    DownloadSessionInfo,
    DownloadSessionUrlFlat,
    DownloadSessionUrlJob,
    DownloadSessionUrlNode,
    DownloadSessionUrlState,
} from '../../api/types';
import type {PanelProps, RecordId} from '../../host/types';
import {RecordInfoView, type RecordInfo} from './RecordInfoView';
import {useRecordMenu} from './recordActions';

const TERMINAL = new Set(['completed', 'failed', 'skipped']);

export type DownloadDisplayState = DownloadSessionUrlState | 'pending-children';

function subtreeWorking(nodes: DownloadSessionUrlNode[]): boolean {
    return nodes.some((node) => !TERMINAL.has(node.state) || subtreeWorking(node.children));
}

/** A completed parent remains pending while descendants are active. */
export function displayState(node: DownloadSessionUrlNode): DownloadDisplayState {
    return node.state === 'completed' && subtreeWorking(node.children) ? 'pending-children' : node.state;
}

export function flatDisplayState(entry: DownloadSessionUrlFlat): DownloadDisplayState {
    const working = entry.urls.some((job) => !TERMINAL.has(job.state));

    return entry.state === 'completed' && working ? 'pending-children' : entry.state;
}

export function stateLabel(state: DownloadDisplayState): string {
    return state === 'pending-children' ? 'pending children' : state;
}

function collectSubtreeRecords(node: DownloadSessionUrlNode, into: number[], seen: Set<number>): void {
    if (node.record_id !== null && !seen.has(node.record_id)) {
        seen.add(node.record_id);
        into.push(node.record_id);
    }
    for (const child of node.children) collectSubtreeRecords(child, into, seen);
}

export function subtreeRecords(node: DownloadSessionUrlNode): number[] {
    const into: number[] = [];
    collectSubtreeRecords(node, into, new Set());
    return into;
}

export function sessionRecords(nodes: DownloadSessionUrlNode[]): number[] {
    const into: number[] = [];
    const seen = new Set<number>();
    for (const node of nodes) collectSubtreeRecords(node, into, seen);
    return into;
}

export function findDownloadNode(
    nodes: DownloadSessionUrlNode[],
    urlId: number,
): DownloadSessionUrlNode | undefined {
    for (const node of nodes) {
        if (node.id === urlId) return node;

        const found = findDownloadNode(node.children, urlId);
        if (found) return found;
    }
}

function countStates(node: DownloadSessionUrlNode, into: Map<string, number>): Map<string, number> {
    const state = displayState(node);

    into.set(state, (into.get(state) ?? 0) + 1);
    for (const child of node.children) countStates(child, into);
    return into;
}

function countImportOutcomes(
    nodes: DownloadSessionUrlNode[],
    into: { processed: number; newlyImported: number },
): void {
    for (const node of nodes) {
        if (node.state === 'completed' && node.record_id !== null) {
            if (node.note === null) into.newlyImported += 1;
            else into.processed += 1;
        }

        countImportOutcomes(node.children, into);
    }
}

export function summarizeDownloadSession(nodes: DownloadSessionUrlNode[]) {
    const records = new Set<number>();
    const newRecords = new Set<number>();
    const counts = {pending: 0, processing: 0, failed: 0, skipped: 0};
    const remaining = [...nodes];

    while (remaining.length > 0) {
        const node = remaining.pop()!;
        remaining.push(...node.children);
        if (node.state !== 'completed') counts[node.state] += 1;

        if (node.record_id === null) continue;

        records.add(node.record_id);
        if (node.state === 'completed' && !isImportedMarker(node.note)) newRecords.add(node.record_id);
    }

    return {
        files: records.size,
        newlyImported: newRecords.size,
        existing: records.size - newRecords.size,
        ...counts,
    };
}

function DownloadSessionSummary({nodes}: { nodes: DownloadSessionUrlNode[] | undefined }) {
    const counts = useMemo(() => nodes === undefined ? undefined : summarizeDownloadSession(nodes), [nodes]);
    if (counts === undefined) return <p className="download-session-summary-loading muted">Loading summary…</p>;

    return (
        <dl className="download-session-summary" aria-label="Download session summary">
            <div title="Unique files identified so far. Pending URLs are counted separately.">
                <dt>Files found</dt>
                <dd>{counts.files.toLocaleString()}</dd>
            </div>
            <div className="download-session-summary-new" title="Unique files newly imported during this session.">
                <dt>New</dt>
                <dd>{counts.newlyImported.toLocaleString()}</dd>
            </div>
            <div title="Unique files that were already in the library.">
                <dt>Existing</dt>
                <dd>{counts.existing.toLocaleString()}</dd>
            </div>
            {counts.pending > 0 && <div>
                <dt>URLs queued</dt>
                <dd>{counts.pending.toLocaleString()}</dd>
            </div>}
            {counts.processing > 0 && <div>
                <dt>URLs active</dt>
                <dd>{counts.processing.toLocaleString()}</dd>
            </div>}
            {counts.failed > 0 && <div className="download-session-summary-failed">
                <dt>URLs failed</dt>
                <dd>{counts.failed.toLocaleString()}</dd>
            </div>}
            {counts.skipped > 0 && <div>
                <dt>URLs skipped</dt>
                <dd>{counts.skipped.toLocaleString()}</dd>
            </div>}
        </dl>
    );
}

/** A request that never got a response has no status to show. */
export function statusLabel(status: number | null): string {
    if (status === null) return 'no response';

    const phrase = HTTP_STATUS_PHRASES[status];
    return phrase === undefined ? `HTTP ${status}` : `${status} ${phrase}`;
}

const HTTP_STATUS_PHRASES: Record<number, string> = {
    100: 'Continue',
    101: 'Switching Protocols',
    200: 'OK',
    201: 'Created',
    202: 'Accepted',
    204: 'No Content',
    206: 'Partial Content',
    301: 'Moved Permanently',
    302: 'Found',
    304: 'Not Modified',
    307: 'Temporary Redirect',
    308: 'Permanent Redirect',
    400: 'Bad Request',
    401: 'Unauthorized',
    403: 'Forbidden',
    404: 'Not Found',
    405: 'Method Not Allowed',
    408: 'Request Timeout',
    409: 'Conflict',
    410: 'Gone',
    413: 'Content Too Large',
    414: 'URI Too Long',
    415: 'Unsupported Media Type',
    418: "I'm a Teapot",
    422: 'Unprocessable Content',
    429: 'Too Many Requests',
    500: 'Internal Server Error',
    501: 'Not Implemented',
    502: 'Bad Gateway',
    503: 'Service Unavailable',
    504: 'Gateway Timeout',
};

export type StatusFilter = readonly (number | null)[];

export function toggleStatus(selected: StatusFilter, status: number | null): (number | null)[] {
    const next = selected.filter((entry) => entry !== status);

    if (next.length === selected.length) next.push(status);
    return next.sort((a, b) => (a ?? Number.MAX_SAFE_INTEGER) - (b ?? Number.MAX_SAFE_INTEGER));
}

function shortUrl(url: string): string {
    try {
        const parsed = new URL(url);
        return `${parsed.host}${parsed.pathname}${parsed.search}`;
    } catch {
        return url;
    }
}

function openDownloadUrl(url: string): void {
    window.open(url, '_blank', 'noopener,noreferrer');
}

function isKnownUrlMarker(note: string | null): boolean {
    return note !== null && /^already (seen|in the database|imported earlier in this session)/i.test(note);
}

function isImportedMarker(note: string | null): boolean {
    return note !== null && /^already (in the database|imported earlier in this session)/i.test(note);
}

function rowStatusLabel(
    state: DownloadDisplayState,
    note: string | null,
): string {
    if (state === 'skipped' && note !== null && /^already seen/i.test(note)) {
        return 'skipped (Already Seen)';
    }

    return stateLabel(state);
}

function downloadRowRank(state: DownloadDisplayState, note: string | null): number {
    if (state === 'pending' || state === 'processing' || state === 'pending-children') return 0;
    if (state === 'skipped' && note !== null && /^already seen/i.test(note)) return 3;
    if (state === 'completed') return 1;
    return 2;
}

function completedUrlGroup(url: string): string {
    try {
        return new URL(url).hostname.toLowerCase().replace(/^www\./, '');
    } catch {
        return url.toLowerCase();
    }
}

function sortDownloadRows<T extends DownloadSessionUrlJob>(
    rows: T[],
    getState: (row: T) => DownloadDisplayState,
): T[] {
    return rows
        .map((row, index) => ({row, index}))
        .sort((left, right) => (
            downloadRowRank(getState(left.row), left.row.note) - downloadRowRank(getState(right.row), right.row.note)
            || (getState(left.row) === 'completed' && getState(right.row) === 'completed'
                ? completedUrlGroup(left.row.url).localeCompare(completedUrlGroup(right.row.url))
                || left.row.url.localeCompare(right.row.url)
                : 0)
            || left.index - right.index
        ))
        .map(({row}) => row);
}

export function parseNewlineUrlList(value: string): string[] {
    return value
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => line.length > 0);
}

interface ErrorLogProps {
    count: number;
    log: DownloadSessionErrorLog | undefined;
    selected: StatusFilter;
    open: boolean;
    onToggle: () => void;
    onToggleStatus: (status: number | null) => void;
    onClearStatuses: () => void;
}

function ErrorLog({count, log, selected, open, onToggle, onToggleStatus, onClearStatuses}: ErrorLogProps) {
    return (
        <div className="download-error-log">
            <button
                type="button"
                className={`download-error-toggle${count > 0 ? ' has-errors' : ''}`}
                disabled={count === 0}
                onClick={onToggle}
            >
                {open ? '▾' : '▸'} Errors ({count})
            </button>
            {open && log !== undefined && (
                <>
                    <div className="download-error-filters">
                        <button
                            type="button"
                            className={selected.length === 0 ? 'is-active' : ''}
                            onClick={onClearStatuses}
                        >
                            All
                        </button>
                        {log.statuses.map((tally) => (
                            <button
                                key={statusLabel(tally.status)}
                                type="button"
                                className={selected.includes(tally.status) ? 'is-active' : ''}
                                onClick={() => onToggleStatus(tally.status)}
                            >
                                {statusLabel(tally.status)} ({tally.count})
                            </button>
                        ))}
                    </div>
                    {log.errors.length === 0 ? (
                        <p className="muted">Nothing logged for this filter.</p>
                    ) : (
                        <ul className="download-error-list">
                            {log.errors.map((entry: DownloadSessionError) => (
                                <li key={entry.id} className="download-error-item">
                                    <div className="download-error-head">
                                        <span className="download-error-status">{statusLabel(entry.status)}</span>
                                        <time className="download-error-time"
                                              dateTime={new Date(entry.occurred_at * 1000).toISOString()}>
                                            {new Date(entry.occurred_at * 1000).toLocaleTimeString()}
                                        </time>
                                    </div>
                                    <span className="download-error-url" title={entry.url}>{shortUrl(entry.url)}</span>
                                    {entry.message && <pre className="download-error-message">{entry.message}</pre>}
                                </li>
                            ))}
                        </ul>
                    )}
                </>
            )}
        </div>
    );
}

interface TreeRowProps {
    node: DownloadSessionUrlNode;
    depth: number;
    selectedId: number | null;
    expanded: Set<number>;
    info: Record<number, RecordInfo>;
    thumbnailUrl: (id: RecordId, size?: number) => string;
    onToggle: (id: number) => void;
    onSelect: (node: DownloadSessionUrlNode) => void;
    onRetry: (id: number) => void;
    onOpenMedia: (recordId: number) => void;
    onRecordMenu: (event: React.MouseEvent, recordId: number) => void;
}

function TreeRow({
                     node,
                     depth,
                     selectedId,
                     expanded,
                     info,
                     thumbnailUrl,
                     onToggle,
                     onSelect,
                     onRetry,
                     onOpenMedia,
                     onRecordMenu,
                 }: TreeRowProps) {
    const open = expanded.has(node.id);
    const hasChildren = node.children.length > 0;
    const expandable = hasChildren || node.record_id !== null;
    const state = displayState(node);

    return (
        <>
            <li
                className={`download-tree-row state-${state}${selectedId === node.id ? ' is-selected' : ''}`}
                style={{paddingLeft: `${0.35 + depth * 0.85}rem`}}
            >
                <button
                    type="button"
                    className="download-tree-twisty"
                    disabled={!expandable}
                    aria-label={open ? 'Collapse' : 'Expand'}
                    onClick={() => onToggle(node.id)}
                >
                    {expandable ? (open ? '▾' : '▸') : '·'}
                </button>
                <button
                    type="button"
                    className="download-tree-url"
                    title={node.url}
                    onClick={(event) => {
                        if (event.ctrlKey) {
                            openDownloadUrl(node.url);
                            return;
                        }
                        onSelect(node);
                    }}
                    onDoubleClick={() => openDownloadUrl(node.url)}
                >
                    <span className="download-tree-url-text">{shortUrl(node.url)}</span>
                    {isImportedMarker(node.note) && <span className="download-tree-url-marker">Already Imported</span>}
                </button>
                {node.record_id !== null && <span className="download-tree-record">#{node.record_id}</span>}
                <span className="download-tree-state">{rowStatusLabel(state, node.note)}</span>
                {TERMINAL.has(node.state) && (
                    <button type="button" className="download-tree-retry" onClick={() => onRetry(node.id)}>
                        Retry
                    </button>
                )}
            </li>
            {node.error && (
                <li className="download-tree-detail" style={{paddingLeft: `${1.9 + depth * 0.85}rem`}}>
                    <pre className="download-tree-error">{node.error}</pre>
                </li>
            )}
            {node.note && !isKnownUrlMarker(node.note) && (
                <li className="download-tree-detail" style={{paddingLeft: `${1.9 + depth * 0.85}rem`}}>
                    <span className="muted">{node.note}</span>
                </li>
            )}
            {open && node.record_id !== null && (
                <li className="download-tree-import" style={{paddingLeft: `${1.9 + depth * 0.85}rem`}}>
                    <img
                        className="download-tree-thumb"
                        src={thumbnailUrl(node.record_id, 128)}
                        alt={`Thumbnail of record ${node.record_id}`}
                        loading="lazy"
                        decoding="async"
                        draggable={false}
                        onClick={() => onOpenMedia(node.record_id!)}
                        onContextMenu={(event) => onRecordMenu(event, node.record_id!)}
                    />
                    <div className="download-tree-import-info">
                        <button type="button" className="download-tree-record"
                                onClick={() => onOpenMedia(node.record_id!)}>
                            Record #{node.record_id}
                        </button>
                        {info[node.record_id] ? (
                            <RecordInfoView info={info[node.record_id]!}/>
                        ) : (
                            <p className="muted">Loading record info...</p>
                        )}
                    </div>
                </li>
            )}
            {open && sortDownloadRows(node.children, displayState).map((child) => (
                <TreeRow
                    key={child.id}
                    node={child}
                    depth={depth + 1}
                    selectedId={selectedId}
                    expanded={expanded}
                    info={info}
                    thumbnailUrl={thumbnailUrl}
                    onToggle={onToggle}
                    onSelect={onSelect}
                    onRetry={onRetry}
                    onOpenMedia={onOpenMedia}
                    onRecordMenu={onRecordMenu}
                />
            ))}
        </>
    );
}

function DownloadsPanel({host}: PanelProps) {
    const [sessions, setSessions] = useState<DownloadSessionInfo[]>([]);
    const [trees, setTrees] = useState<Record<number, DownloadSessionUrlNode[]>>({});
    const [flat, setFlat] = useState<Record<number, DownloadSessionUrlFlat[]>>({});
    const [errorLogs, setErrorLogs] = useState<Record<number, DownloadSessionErrorLog>>({});
    const [errorFilters, setErrorFilters] = useState<Record<number, StatusFilter>>({});
    const [openErrors, setOpenErrors] = useState<Set<number>>(new Set());
    const [expanded, setExpanded] = useState<Set<number>>(new Set());
    const [selectedId, setSelectedId] = useState<number | null>(null);
    const [selectedSessionId, setSelectedSessionId] = useState<number | null>(null);
    const [flatten, setFlatten] = useState(false);
    const [draft, setDraft] = useState('');
    const [busy, setBusy] = useState(false);
    const [live, setLive] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [info, setInfo] = useState<Record<number, RecordInfo>>({});
    const {openRecordMenu, recordMenu} = useRecordMenu(host);

    const showRecords = useCallback((ids: number[], label: string) => {
        host.results.set({ids: Int32Array.from(new Set(ids)), queryMs: 0, query: [label]});
        host.selection.set([]);
    }, [host]);
    const selectedIdRef = useRef<number | null>(null);
    const selectedSessionIdRef = useRef<number | null>(null);
    const showRecordsRef = useRef(showRecords);
    showRecordsRef.current = showRecords;

    const refresh = useCallback(async () => {
        const listed = await api.downloadSessions.list();
        setSessions(listed);
    }, []);

    useEffect(() => watchDownloadSessions({
        onOpen: () => {
            setLive(true);
            setError(null);
        },
        onSession: (session, urls) => {
            setSessions((previous) => {
                const rest = previous.filter((entry) => entry.id !== session.id);
                return [session, ...rest].sort((a, b) => b.last_used_at - a.last_used_at || b.id - a.id);
            });
            setTrees((previous) => ({...previous, [session.id]: urls}));

            if (selectedSessionIdRef.current === session.id) {
                showRecordsRef.current(sessionRecords(urls), `download session: ${session.name}`);
                return;
            }

            const selectedId = selectedIdRef.current;
            if (selectedId === null) return;

            const selectedNode = findDownloadNode(urls, selectedId);
            if (!selectedNode) return;

            showRecordsRef.current(
                subtreeRecords(selectedNode),
                `download: ${shortUrl(selectedNode.url)}`,
            );
        },
        onRemoved: (sessionId) => {
            setSessions((previous) => previous.filter((entry) => entry.id !== sessionId));
            setTrees((previous) => {
                const next = {...previous};
                delete next[sessionId];
                return next;
            });
            if (selectedSessionIdRef.current === sessionId) {
                selectedSessionIdRef.current = null;
                setSelectedSessionId(null);
            }
        },
        onError: (message) => {
            setLive(false);
            setError(message);
        },
    }), []);

    useEffect(() => {
        if (!flatten) return;
        let cancelled = false;

        const load = async () => {
            const next: Record<number, DownloadSessionUrlFlat[]> = {};
            for (const session of sessions) next[session.id] = await api.downloadSessions.urlsFlat(session.id);
            if (!cancelled) setFlat(next);
        };

        void load().catch((caught) => {
            if (!cancelled) setError(caught instanceof Error ? caught.message : String(caught));
        });

        return () => {
            cancelled = true;
        };
    }, [flatten, sessions, trees]);

    useEffect(() => {
        if (openErrors.size === 0) return;
        let cancelled = false;

        const load = async () => {
            const next: Record<number, DownloadSessionErrorLog> = {};
            for (const sessionId of openErrors) {
                next[sessionId] = await api.downloadSessions.errors(sessionId, errorFilters[sessionId] ?? []);
            }
            if (!cancelled) setErrorLogs((previous) => ({...previous, ...next}));
        };

        void load().catch((caught) => {
            if (!cancelled) setError(caught instanceof Error ? caught.message : String(caught));
        });

        return () => {
            cancelled = true;
        };
    }, [openErrors, errorFilters, trees]);

    const toggleErrors = useCallback((sessionId: number) => {
        setOpenErrors((previous) => {
            const next = new Set(previous);
            if (!next.delete(sessionId)) next.add(sessionId);
            return next;
        });
    }, []);

    const toggleErrorStatus = useCallback((sessionId: number, status: number | null) => {
        setErrorFilters((previous) => ({...previous, [sessionId]: toggleStatus(previous[sessionId] ?? [], status)}));
    }, []);

    const clearErrorStatuses = useCallback((sessionId: number) => {
        setErrorFilters((previous) => ({...previous, [sessionId]: []}));
    }, []);

    const queue = useCallback(async () => {
        const urls = parseNewlineUrlList(draft);
        if (urls.length === 0) return;

        setBusy(true);
        setError(null);
        const failedUrls: string[] = [];
        const errors: string[] = [];
        try {
            for (const url of urls) {
                try {
                    const created = await api.downloadSessions.submitUrlSession(url);
                    setExpanded((previous) => new Set(previous).add(created.url.id));
                } catch (caught) {
                    failedUrls.push(url);
                    errors.push(`${url}: ${caught instanceof Error ? caught.message : String(caught)}`);
                }
            }

            try {
                await refresh();
            } catch (caught) {
                errors.push(`Unable to refresh downloads: ${caught instanceof Error ? caught.message : String(caught)}`);
            }

            if (failedUrls.length > 0) {
                setDraft(failedUrls.join('\n'));
                setError(`Failed to queue ${failedUrls.length} URL${failedUrls.length === 1 ? '' : 's'}: ${errors.join('; ')}`);
                return;
            }

            setDraft('');
            host.ui.toast(`Queued ${urls.length} URL${urls.length === 1 ? '' : 's'}`, {kind: 'success'});
            if (errors.length > 0) setError(errors.join('; '));
        } finally {
            setBusy(false);
        }
    }, [draft, host, refresh]);

    const retry = useCallback(async (sessionId: number, urlId: number) => {
        setError(null);
        try {
            await api.downloadSessions.retryUrl(sessionId, urlId);
            await refresh();
        } catch (caught) {
            setError(caught instanceof Error ? caught.message : String(caught));
        }
    }, [refresh]);

    const destroy = useCallback(async (session: DownloadSessionInfo) => {
        setError(null);
        try {
            await api.downloadSessions.destroy(session.id);
            if (selectedSessionIdRef.current === session.id) {
                selectedSessionIdRef.current = null;
                setSelectedSessionId(null);
            }
            await refresh();
        } catch (caught) {
            setError(caught instanceof Error ? caught.message : String(caught));
        }
    }, [refresh]);

    const toggle = useCallback((id: number) => {
        setExpanded((previous) => {
            const next = new Set(previous);
            if (!next.delete(id)) next.add(id);
            return next;
        });
    }, []);

    const openMedia = useCallback((recordId: number) => {
        host.selection.set([recordId]);
    }, [host]);

    useEffect(() => {
        const wanted: number[] = [];
        const walk = (node: DownloadSessionUrlNode) => {
            if (expanded.has(node.id) && node.record_id !== null && info[node.record_id] === undefined) {
                wanted.push(node.record_id);
            }
            for (const child of node.children) walk(child);
        };
        for (const roots of Object.values(trees)) for (const root of roots) walk(root);
        if (wanted.length === 0) return;

        let cancelled = false;
        void host.records.getMetadata(wanted).then((loaded) => {
            if (cancelled) return;
            setInfo((previous) => {
                const next = {...previous};
                for (const metadata of loaded.records) next[metadata.record_id] = metadata;
                return next;
            });
        }).catch(() => { /* a missing record just leaves the row without its detail block */
        });

        return () => {
            cancelled = true;
        };
    }, [expanded, trees, info, host]);

    const select = useCallback((node: DownloadSessionUrlNode) => {
        selectedSessionIdRef.current = null;
        setSelectedSessionId(null);
        selectedIdRef.current = node.id;
        setSelectedId(node.id);
        showRecords(subtreeRecords(node), `download: ${shortUrl(node.url)}`);
    }, [showRecords]);

    const selectFlat = useCallback((entry: DownloadSessionUrlFlat) => {
        selectedSessionIdRef.current = null;
        setSelectedSessionId(null);
        selectedIdRef.current = entry.id;
        setSelectedId(entry.id);
        showRecords(entry.record_ids, `download: ${shortUrl(entry.url)}`);
    }, [showRecords]);

    const selectSession = useCallback(async (session: DownloadSessionInfo) => {
        selectedIdRef.current = null;
        setSelectedId(null);
        selectedSessionIdRef.current = session.id;
        setSelectedSessionId(session.id);
        setError(null);
        try {
            const records = await api.downloadSessions.records(session.id);
            if (selectedSessionIdRef.current !== session.id) return;
            showRecords(records.record_ids, `download session: ${session.name}`);
        } catch (caught) {
            setError(caught instanceof Error ? caught.message : String(caught));
        }
    }, [showRecords]);

    const summary = useMemo(() => {
        const counts = new Map<string, number>();
        for (const roots of Object.values(trees)) for (const root of roots) countStates(root, counts);
        return [...counts.entries()].filter(([state]) => state !== 'completed').sort(([a], [b]) => a.localeCompare(b));
    }, [trees]);
    const importOutcomes = useMemo(() => {
        const counts = {processed: 0, newlyImported: 0};
        for (const roots of Object.values(trees)) countImportOutcomes(roots, counts);
        return counts;
    }, [trees]);

    return (
        <div className="downloads-panel">
            <div className="download-session-row">
        <textarea
            value={draft}
            onChange={(event) => setDraft(event.target.value)}
            placeholder="Paste one URL per line"
            disabled={busy}
        />
                <button type="button" disabled={busy || parseNewlineUrlList(draft).length === 0}
                        onClick={() => void queue()}>Download URL(s)
                </button>
            </div>
            <div className="download-tree-toolbar">
                <label>
                    <input type="checkbox" checked={flatten} onChange={(event) => setFlatten(event.target.checked)}/>
                    Flat list
                </label>
                <span className={live ? 'download-tree-live' : 'muted'}>{live ? 'Live' : 'Reconnecting'}</span>
                <span
                    className="download-tree-tally">{importOutcomes.processed} Processed, {importOutcomes.newlyImported} New</span>
                {summary.map(([state, count]) => (
                    <span key={state} className={`download-tree-tally state-${state}`}>
                        {count} {stateLabel(state as DownloadDisplayState)}
                    </span>
                ))}
            </div>

            <div className="download-tree-scroll">
                {sessions.length === 0 && <p className="muted">No downloads yet. Paste a URL above to start one.</p>}

                {sessions.map((session) => (
                    <section key={session.id}
                             className={`download-tree-session${selectedSessionId === session.id ? ' is-selected' : ''}`}>
                        <header className="download-tree-session-head">
                            <button
                                type="button"
                                className="download-tree-session-select"
                                title={`Show all records imported by ${session.name}`}
                                onClick={() => void selectSession(session)}
                            >
                                {session.name}
                            </button>
                            <button type="button" onClick={() => void destroy(session)}>Delete</button>
                        </header>
                        <DownloadSessionSummary nodes={trees[session.id]}/>
                        <ErrorLog
                            count={session.error_count}
                            log={errorLogs[session.id]}
                            selected={errorFilters[session.id] ?? []}
                            open={openErrors.has(session.id)}
                            onToggle={() => toggleErrors(session.id)}
                            onToggleStatus={(status) => toggleErrorStatus(session.id, status)}
                            onClearStatuses={() => clearErrorStatuses(session.id)}
                        />
                        {flatten ? (
                            <ul className="download-tree">
                                {sortDownloadRows(flat[session.id] ?? [], flatDisplayState).map((entry) => (
                                    <li key={entry.id} className="download-tree-flat">
                                        <button
                                            type="button"
                                            className={`download-tree-url${selectedId === entry.id ? ' is-selected' : ''}`}
                                            title={entry.url}
                                            onClick={(event) => {
                                                if (event.ctrlKey) {
                                                    openDownloadUrl(entry.url);
                                                    return;
                                                }
                                                selectFlat(entry);
                                            }}
                                            onDoubleClick={() => openDownloadUrl(entry.url)}
                                        >
                                            <span className="download-tree-url-text">{shortUrl(entry.url)}</span>
                                            {isImportedMarker(entry.note) &&
                                                <span className="download-tree-url-marker">Already Imported</span>}
                                        </button>
                                        <span className="download-tree-state">
                                            {rowStatusLabel(flatDisplayState(entry), entry.note)}
                                        </span>
                                        <p className="muted">
                                            {entry.urls.length} discovered, {entry.record_ids.length} imported
                                        </p>
                                        <ul className="download-tree-flat-urls">
                                            {sortDownloadRows(entry.urls, (child) => child.state).map((child: DownloadSessionUrlJob) => (
                                                <li key={child.id} className={`state-${child.state}`}>
                                                    <span
                                                        className="grow download-tree-child-url"
                                                        title={child.url}
                                                        onClick={(event) => {
                                                            if (event.ctrlKey) openDownloadUrl(child.url);
                                                        }}
                                                        onDoubleClick={() => openDownloadUrl(child.url)}
                                                    >
                                                        <span
                                                            className="download-tree-url-text">{shortUrl(child.url)}</span>
                                                        {isImportedMarker(child.note) && <span
                                                            className="download-tree-url-marker">Already Imported</span>}
                                                    </span>
                                                    {child.record_id !== null && <span
                                                        className="download-tree-record">#{child.record_id}</span>}
                                                    <span
                                                        className="download-tree-state">{rowStatusLabel(child.state, child.note)}</span>
                                                </li>
                                            ))}
                                        </ul>
                                    </li>
                                ))}
                            </ul>
                        ) : (
                            <ul className="download-tree">
                                {sortDownloadRows(trees[session.id] ?? [], displayState).map((root) => (
                                    <TreeRow
                                        key={root.id}
                                        node={root}
                                        depth={0}
                                        selectedId={selectedId}
                                        expanded={expanded}
                                        info={info}
                                        thumbnailUrl={(id, size) => host.records.thumbnailUrl(id, size)}
                                        onToggle={toggle}
                                        onSelect={select}
                                        onRetry={(id) => void retry(session.id, id)}
                                        onOpenMedia={openMedia}
                                        onRecordMenu={(event, recordId) => openRecordMenu(event, [recordId])}
                                    />
                                ))}
                            </ul>
                        )}
                    </section>
                ))}
            </div>

            {error && <p className="error">{error}</p>}
            {recordMenu}
        </div>
    );
}

export const downloadsPanel = {
    type: 'downloads',
    title: 'Downloads',
    description: 'Queue URLs and follow what the downloader found, imported and skipped.',
    component: DownloadsPanel,
    configVersion: 1,
} as const;
