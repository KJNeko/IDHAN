import {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import {DownloadOptions} from './DownloadOptions';
import {DownloadRateLimits} from './DownloadRateLimits';
import {DownloadParserCache} from './DownloadParserCache';
import {ParserInfo} from './ParserInfo';
import type {DownloadSettings} from '../../api/types';
import {api, watchDownloadSessions} from '../../api/client';
import type {
    DownloadFailureStage,
    DownloadSessionError,
    DownloadSessionErrorLog,
    DownloadSessionInfo,
    DownloadSessionUrlFlat,
    DownloadSessionUrlJob,
    DownloadSessionUrlNode,
    DownloadSessionUrlState,
} from '../../api/types';
import type {PanelProps, RecordId} from '../../host/types';
import {forgetMetadata} from '../../host/metadataCache';
import {RecordInfoView, type RecordInfo} from './RecordInfoView';
import {useRecordMenu} from './recordActions';
import {ImportStorageNotice, useImportStorage} from './importStorage';

const TERMINAL = new Set<DownloadSessionUrlState>([
    'completed', 'imported', 'imported_previously', 'skipped_no_url_class', 'skipped_already_seen',
    'skipped_already_imported', 'failed', 'cancelled',
]);

/** The states the server lets a URL be retried from. */
const RETRYABLE = new Set<DownloadSessionUrlState>(['completed', 'imported', 'imported_previously', 'failed']);

const ACTIVE = new Set<DownloadSessionUrlState>(['downloading', 'processing', 'importing']);

const SKIPPED = new Set<DownloadSessionUrlState>([
    'skipped_no_url_class', 'skipped_already_seen', 'skipped_already_imported',
]);

const STATE_LABELS: Partial<Record<string, string>> = {
    'pending-children': 'pending children',
    imported_previously: 'already imported',
    skipped_no_url_class: 'skipped (No URL Class)',
    skipped_already_seen: 'skipped (Already Seen)',
    skipped_already_imported: 'skipped (Already Imported)',
};

const STAGE_LABELS: Record<DownloadFailureStage, string> = {
    route: 'No URL Class',
    download: 'Download Failed',
    parser: 'Parser Error',
    import: 'Import Failed',
    closed: 'Session Closed',
};

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
    return STATE_LABELS[state] ?? state;
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
        if (node.state === 'imported') into.newlyImported += 1;

        if (node.state === 'imported_previously') into.processed += 1;


        countImportOutcomes(node.children, into);
    }
}

export function summarizeDownloadSession(nodes: DownloadSessionUrlNode[]) {
    const records = new Set<number>();
    const newRecords = new Set<number>();
    const sizes = new Map<number, number>();
    const counts = {pending: 0, processing: 0, failed: 0, skipped: 0};
    const remaining = [...nodes];

    while (remaining.length > 0) {
        const node = remaining.pop()!;
        remaining.push(...node.children);
        if (node.state === 'pending') counts.pending += 1;

        if (ACTIVE.has(node.state)) counts.processing += 1;

        if (node.state === 'failed') counts.failed += 1;

        if (SKIPPED.has(node.state)) counts.skipped += 1;

        if (node.record_id === null) continue;

        records.add(node.record_id);
        if (node.size !== null) sizes.set(node.record_id, node.size);

        if (node.state === 'imported') newRecords.add(node.record_id);
    }

    // A record counts as new if any URL imported it, even when other URLs matched it afterwards.
    let newBytes = 0;
    let existingBytes = 0;
    for (const [record, size] of sizes) {
        if (newRecords.has(record)) newBytes += size;
        else existingBytes += size;
    }

    return {
        files: records.size,
        newlyImported: newRecords.size,
        existing: records.size - newRecords.size,
        newBytes,
        existingBytes,
        ...counts,
    };
}

function formatDownloadBytes(bytes: number): string {
    if (bytes < 1000) return `${bytes} B`;
    if (bytes < 1_000_000) return `${(bytes / 1000).toFixed(1)} KB`;
    if (bytes < 1_000_000_000) return `${(bytes / 1_000_000).toFixed(1)} MB`;
    return `${(bytes / 1_000_000_000).toFixed(1)} GB`;
}

function formatNetworkSpeed(bytes: number): string {
    if (bytes < 1000) return `${bytes} B/s`;
    if (bytes < 1_000_000) return `${(bytes / 1000).toFixed(1)} KB/s`;
    return `${(bytes / 1_000_000).toFixed(1)} MB/s`;
}

function DownloadSessionSummary({nodes}: { nodes: DownloadSessionUrlNode[] | undefined }) {
    const counts = useMemo(() => nodes === undefined ? undefined : summarizeDownloadSession(nodes), [nodes]);
    if (counts === undefined) return <p className="download-session-summary-loading muted">Loading summary…</p>;

    return (
        <dl className="download-session-summary" aria-label="Download session summary">
			<div>
                <dt>Files found</dt>
                <dd>{counts.files.toLocaleString()}</dd>
            </div>
			<div className="download-session-summary-new">
				<dt>New files</dt>
                <dd>{counts.newlyImported.toLocaleString()} ({formatDownloadBytes(counts.newBytes)})</dd>
            </div>
			<div>
				<dt>Existing files</dt>
                <dd>{counts.existing.toLocaleString()} ({formatDownloadBytes(counts.existingBytes)})</dd>
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
		return `${parsed.host}${parsed.pathname}${parsed.search}${parsed.hash}`;
    } catch {
        return url;
    }
}

function openDownloadUrl(url: string): void {
    window.open(url, '_blank', 'noopener,noreferrer');
}

function rowStatusLabel(state: DownloadDisplayState, stage: DownloadFailureStage | null): string {
    if (state === 'failed' && stage !== null) return `failed (${STAGE_LABELS[stage]})`;

    return stateLabel(state);
}

function transferLabel(row: DownloadSessionUrlJob): string | null {
    if (row.state !== 'downloading' || row.bytes === undefined) return null;

    if (row.total === undefined) return formatDownloadBytes(row.bytes);

    return `${formatDownloadBytes(row.bytes)} / ${formatDownloadBytes(row.total)}`;
}

function isSettledImport(state: DownloadDisplayState): boolean {
    return state === 'completed' || state === 'imported' || state === 'imported_previously';
}

function downloadRowRank(state: DownloadDisplayState): number {
    if (state === 'pending' || state === 'pending-children') return 0;
    if (ACTIVE.has(state as DownloadSessionUrlState)) return 0;
    if (state === 'skipped_already_seen') return 3;
    if (isSettledImport(state)) return 1;
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
            downloadRowRank(getState(left.row)) - downloadRowRank(getState(right.row))
            || (isSettledImport(getState(left.row)) && isSettledImport(getState(right.row))
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
	failures?: DownloadSessionUrlJob[];
    log: DownloadSessionErrorLog | undefined;
    loadError?: string;
    selected: StatusFilter;
    open: boolean;
    onToggle: () => void;
    onToggleStatus: (status: number | null) => void;
    onClearStatuses: () => void;
}

export function ErrorLog({
                             count,
							 failures = [],
                             log,
                             loadError,
                             selected,
                             open,
                             onToggle,
                             onToggleStatus,
                             onClearStatuses
                         }: ErrorLogProps) {
    const [page, setPage] = useState(0);
	const [failurePage, setFailurePage] = useState(0);
	useEffect(() => setFailurePage(0), [open]);
    const filterKey = JSON.stringify(selected);
    useEffect(() => setPage(0), [filterKey, open]);
    const pageSize = 50;
    const total = log?.errors.length ?? 0;
    const lastPage = Math.max(0, Math.ceil(total / pageSize) - 1);
    const currentPage = Math.min(page, lastPage);
    const start = currentPage * pageSize;
	const lastFailurePage = Math.max(0, Math.ceil(failures.length / pageSize) - 1);
	const currentFailurePage = Math.min(failurePage, lastFailurePage);
	const failureStart = currentFailurePage * pageSize;
	const totalCount = count + failures.length;
    return (
        <div className="download-error-log">
            <button
                type="button"
				className={`download-error-toggle${totalCount > 0 ? ' has-errors' : ''}`}
				disabled={totalCount === 0 && !open}
                aria-expanded={open}
                onClick={onToggle}
            >
				{open ? '▾' : '▸'} Errors ({totalCount})
            </button>
			{open && failures.length > 0 && <section aria-label="Failed URLs">
                <p className="muted">Failed URLs ({failures.length})</p>
                <ul className="download-error-list">
					{failures.slice(failureStart, failureStart + pageSize).map(node => (
						<li key={node.id} className="download-error-item">
							<div className="download-error-head">
								<span className="download-error-status">{rowStatusLabel('failed', node.stage)}</span>
							</div>
							<span className="download-error-url" title={node.url}>{shortUrl(node.url)}</span>
							<details className="download-error-detail">
								<summary>{(node.error || 'The download failed without a reason').split('\n')[0]?.slice(0, 160)}</summary>
								<pre
							        className="download-error-message">{node.error || 'The download failed without a reason'}</pre>
							</details>
						</li>
					))}
                </ul>
				{failures.length > pageSize && <nav className="download-error-pages" aria-label="Failed URL pages">
                    <button type="button" disabled={currentFailurePage === 0}
                            onClick={() => setFailurePage(currentFailurePage - 1)}>Previous
                    </button>
                    <span className="muted" role="status">
                        {failureStart + 1}–{Math.min(failureStart + pageSize, failures.length)} of {failures.length.toLocaleString()}
                    </span>
                    <button type="button" disabled={currentFailurePage === lastFailurePage}
                            onClick={() => setFailurePage(currentFailurePage + 1)}>Next
                    </button>
                </nav>}
            </section>}
			{open && <p className="muted">Request errors ({count})</p>}
            {open && loadError && <p className="error" role="alert">{loadError}</p>}
            {open && !loadError && log === undefined && <p className="muted" role="status">Loading errors…</p>}
            {open && log !== undefined && (
                <>
                    <div className="download-error-filters">
                        <button
                            type="button"
                            className={selected.length === 0 ? 'is-active' : ''}
                            aria-pressed={selected.length === 0}
                            onClick={onClearStatuses}
                        >
                            All
                        </button>
                        {log.statuses.map((tally) => (
                            <button
                                key={statusLabel(tally.status)}
                                type="button"
                                className={selected.includes(tally.status) ? 'is-active' : ''}
                                aria-pressed={selected.includes(tally.status)}
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
                            {log.errors.slice(start, start + pageSize).map((entry: DownloadSessionError) => (
                                <li key={entry.id} className="download-error-item">
                                    <div className="download-error-head">
                                        <span className="download-error-status">{statusLabel(entry.status)}</span>
                                        <time className="download-error-time"
                                              dateTime={new Date(entry.occurred_at * 1000).toISOString()}>
                                            {new Date(entry.occurred_at * 1000).toLocaleTimeString()}
                                        </time>
                                    </div>
                                    <span className="download-error-url" title={entry.url}>{shortUrl(entry.url)}</span>
                                    {entry.message && <details className="download-error-detail">
                                        <summary>{entry.message.split('\n')[0]?.slice(0, 160)}</summary>
                                        <pre className="download-error-message">{entry.message}</pre>
                                    </details>}
                                </li>
                            ))}
                        </ul>
                    )}
                    {total > pageSize && <nav className="download-error-pages" aria-label="Error pages">
                        <button type="button" disabled={currentPage === 0}
                                onClick={() => setPage(currentPage - 1)}>Previous
                        </button>
                        <span className="muted"
                              role="status">{start + 1}–{Math.min(start + pageSize, total)} of {total.toLocaleString()}</span>
                        <button type="button" disabled={currentPage === lastPage}
                                onClick={() => setPage(currentPage + 1)}>Next
                        </button>
                    </nav>}
                </>
            )}
        </div>
    );
}

interface TreeRowProps {
    node: DownloadSessionUrlNode;
    depth: number;
    selectedId: number | null;
	expanded: ReadonlySet<number> | undefined;
    info: Record<number, RecordInfo>;
    thumbnailUrl: (id: RecordId, size?: number) => string;
    onToggle: (id: number) => void;
    onSelect: (node: DownloadSessionUrlNode) => void;
    onRetry: (id: number) => void;
    retryDisabled: boolean;
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
                     retryDisabled,
                     onOpenMedia,
                     onRecordMenu,
                 }: TreeRowProps) {
	const open = expanded?.has(node.id) ?? false;
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
                </button>
                {node.record_id !== null && <span className="download-tree-record">#{node.record_id}</span>}
                {node.size !== null && <span className="download-tree-size">{formatDownloadBytes(node.size)}</span>}
                {transferLabel(node) !== null && <span className="download-tree-progress">{transferLabel(node)}</span>}
                <span className="download-tree-state">{rowStatusLabel(state, node.stage)}</span>
                {RETRYABLE.has(node.state) && (
                    <button type="button" className="download-tree-retry" disabled={retryDisabled}
                            onClick={() => onRetry(node.id)}>
                        Retry
                    </button>
                )}
            </li>
            {node.error && (
                <li className="download-tree-detail" style={{paddingLeft: `${1.9 + depth * 0.85}rem`}}>
                    <pre className="download-tree-error">{node.error}</pre>
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
                    retryDisabled={retryDisabled}
                    onOpenMedia={onOpenMedia}
                    onRecordMenu={onRecordMenu}
                />
            ))}
        </>
    );
}

function flattenDownloadRoot(root: DownloadSessionUrlNode): DownloadSessionUrlFlat {
    const urls: DownloadSessionUrlNode[] = [];
    const pending = [...root.children].reverse();
    while (pending.length) {
        const row = pending.pop()!;
        urls.push(row);
        pending.push(...[...row.children].reverse());
    }
    return {...root, urls, record_ids: subtreeRecords(root)};
}

interface DownloadUrlSelection {
	sessionId: number;
	urlId: number;
}

function DownloadsPanel({host}: PanelProps) {
    const storage = useImportStorage(host);
    const {canImport, check: checkStorage} = storage;
    const [sessions, setSessions] = useState<DownloadSessionInfo[]>([]);
    const [trees, setTrees] = useState<Record<number, DownloadSessionUrlNode[]>>({});
    const flat = useMemo(() => {
        const result: Record<number, DownloadSessionUrlFlat[]> = {};
        for (const [id, roots] of Object.entries(trees)) result[Number(id)] = roots.map(flattenDownloadRoot);
        return result;
    }, [trees]);
	const failures = useMemo(() => Object.fromEntries(
		Object.entries(flat).map(([sessionId, roots]) => [sessionId,
			roots.flatMap(root => [root, ...root.urls]).filter(node => node.state === 'failed'),
		]),
	), [flat]);
    const [errorLogs, setErrorLogs] = useState<Record<number, DownloadSessionErrorLog>>({});
    const [errorFilters, setErrorFilters] = useState<Record<number, StatusFilter>>({});
    const [errorLogFailures, setErrorLogFailures] = useState<Record<number, string>>({});
    const [openErrors, setOpenErrors] = useState<Set<number>>(new Set());
	const [expanded, setExpanded] = useState<Record<number, ReadonlySet<number>>>({});
	const [selectedUrl, setSelectedUrl] = useState<DownloadUrlSelection | null>(null);
    const [selectedSessionId, setSelectedSessionId] = useState<number | null>(null);
    const [flatten, setFlatten] = useState(false);
    const [draft, setDraft] = useState('');
    const [downloadSettings, setDownloadSettings] = useState<DownloadSettings>({});
    const [busy, setBusy] = useState(false);
    const [live, setLive] = useState(false);
	const [downloadSpeed, setDownloadSpeed] = useState(0);
	const [sessionDownloadSpeeds, setSessionDownloadSpeeds] = useState<Record<number, number>>({});
    const [streamError, setStreamError] = useState<string | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [info, setInfo] = useState<Record<number, RecordInfo>>({});
    const {openRecordMenu, recordMenu} = useRecordMenu(host);

    const showRecords = useCallback((ids: number[], label: string) => {
        host.results.set({ids: Int32Array.from(new Set(ids)), queryMs: 0, query: [label]});
        host.selection.set([]);
    }, [host]);
    const streamedTrees = useRef<Record<number, DownloadSessionUrlNode[]>>({});
	const selectedUrlRef = useRef<DownloadUrlSelection | null>(null);
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
			setDownloadSpeed(0);
			setSessionDownloadSpeeds({});
            setStreamError(null);
            streamedTrees.current = {};
            setSessions([]);
            setTrees({});
        },
		onBandwidth: (bandwidth) => {
			setDownloadSpeed(bandwidth.download_bytes_per_second);
			setSessionDownloadSpeeds(Object.fromEntries(
				bandwidth.sessions.map(session => [session.session_id, session.download_bytes_per_second]),
			));
		},
        onSession: (session, urls) => {
            const urlsChanged = streamedTrees.current[session.id] !== urls;
            if (urlsChanged) {
                streamedTrees.current[session.id] = urls;
                forgetMetadata(sessionRecords(urls));
                setTrees((previous) => ({...previous, [session.id]: urls}));
            }
            setSessions((previous) => {
                const rest = previous.filter((entry) => entry.id !== session.id);
                return [session, ...rest].sort((a, b) => b.last_used_at - a.last_used_at || b.id - a.id);
            });
            if (!urlsChanged) return;

            if (selectedSessionIdRef.current === session.id) {
                showRecordsRef.current(sessionRecords(urls), `download session: ${session.name}`);
                return;
            }

			const selectedUrl = selectedUrlRef.current;
			if (selectedUrl === null) return;

			if (selectedUrl.sessionId !== session.id) return;

			const selectedNode = findDownloadNode(urls, selectedUrl.urlId);
            if (!selectedNode) return;

            showRecordsRef.current(
                subtreeRecords(selectedNode),
                `download: ${shortUrl(selectedNode.url)}`,
            );
        },
        onRemoved: (sessionId) => {
            delete streamedTrees.current[sessionId];
			setExpanded((previous) => {
				const next = {...previous};
				delete next[sessionId];
				return next;
			});
            setSessions((previous) => previous.filter((entry) => entry.id !== sessionId));
            setTrees((previous) => {
                const next = {...previous};
                delete next[sessionId];
                return next;
            });
			if (selectedUrlRef.current?.sessionId === sessionId) {
				selectedUrlRef.current = null;
				setSelectedUrl(null);
			}

            if (selectedSessionIdRef.current === sessionId) {
                selectedSessionIdRef.current = null;
                setSelectedSessionId(null);
            }
        },
        onError: (message) => {
            setLive(false);
			setDownloadSpeed(0);
			setSessionDownloadSpeeds({});
            setStreamError(message);
        },
    }), []);

    useEffect(() => {
        if (openErrors.size === 0) return;
        let cancelled = false;

        const controller = new AbortController();
        const load = async (sessionId: number) => {
            try {
                const log = await api.downloadSessions.errors(sessionId, errorFilters[sessionId] ?? [], controller.signal);
                if (cancelled) return;
                setErrorLogs(previous => ({...previous, [sessionId]: log}));
                setErrorLogFailures(previous => {
                    const next = {...previous};
                    delete next[sessionId];
                    return next;
                });
            } catch (caught) {
                if (cancelled) return;
                setErrorLogFailures(previous => ({
                    ...previous,
                    [sessionId]: `Could not load errors: ${caught instanceof Error ? caught.message : String(caught)}`
                }));
            }
        };
        void Promise.all([...openErrors].map(load));

        return () => {
            cancelled = true;
            controller.abort();
        };
    }, [openErrors, errorFilters, trees]);

    const toggleErrors = useCallback((sessionId: number) => {
        setOpenErrors((previous) => {
            const next = new Set(previous);
            if (!next.delete(sessionId)) next.add(sessionId);
            return next;
        });
    }, []);

    const clearErrorLog = useCallback((sessionId: number) => {
        setErrorLogs(previous => {
            const next = {...previous};
            delete next[sessionId];
            return next;
        });
        setErrorLogFailures(previous => {
            const next = {...previous};
            delete next[sessionId];
            return next;
        });
    }, []);

    const toggleErrorStatus = useCallback((sessionId: number, status: number | null) => {
        clearErrorLog(sessionId);
        setErrorFilters((previous) => ({...previous, [sessionId]: toggleStatus(previous[sessionId] ?? [], status)}));
    }, [clearErrorLog]);

    const clearErrorStatuses = useCallback((sessionId: number) => {
        clearErrorLog(sessionId);
        setErrorFilters((previous) => ({...previous, [sessionId]: []}));
    }, [clearErrorLog]);

    const queue = useCallback(async () => {
        if (!canImport || busy) return;
        const urls = parseNewlineUrlList(draft);
        if (urls.length === 0) return;

        setBusy(true);
        setError(null);
        const failedUrls: string[] = [];
        const errors: string[] = [];
        try {
            if (!await checkStorage()) return;
            for (const url of urls) {
                try {
                    const created = await api.downloadSessions.submitUrlSession(url, undefined, undefined, downloadSettings);
					setExpanded((previous) => ({
						...previous,
						[created.session.id]: new Set(previous[created.session.id]).add(created.url.id),
					}));
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
    }, [draft, host, refresh, canImport, checkStorage, busy, downloadSettings]);

    const retry = useCallback(async (sessionId: number, urlId: number) => {
        if (!canImport) return;
        setError(null);
        try {
            if (!await checkStorage()) return;
            await api.downloadSessions.retryUrl(sessionId, urlId);
            await refresh();
        } catch (caught) {
            setError(caught instanceof Error ? caught.message : String(caught));
        }
    }, [refresh, canImport, checkStorage]);

    const destroy = useCallback(async (session: DownloadSessionInfo) => {
        setError(null);
        try {
            await api.downloadSessions.destroy(session.id);
			if (selectedUrlRef.current?.sessionId === session.id) {
				selectedUrlRef.current = null;
				setSelectedUrl(null);
			}

            if (selectedSessionIdRef.current === session.id) {
                selectedSessionIdRef.current = null;
                setSelectedSessionId(null);
            }
            await refresh();
        } catch (caught) {
            setError(caught instanceof Error ? caught.message : String(caught));
        }
    }, [refresh]);

	const toggle = useCallback((sessionId: number, id: number) => {
        setExpanded((previous) => {
			const next = new Set(previous[sessionId]);
            if (!next.delete(id)) next.add(id);
			return {...previous, [sessionId]: next};
        });
    }, []);

    const openMedia = useCallback((recordId: number) => {
        host.selection.set([recordId]);
    }, [host]);

    useEffect(() => {
        const wanted = new Set<number>();
		const walk = (node: DownloadSessionUrlNode, sessionExpanded: ReadonlySet<number> | undefined) => {
			if (sessionExpanded?.has(node.id) && node.record_id !== null) {
                wanted.add(node.record_id);
            }
			for (const child of node.children) walk(child, sessionExpanded);
        };
		for (const [sessionId, roots] of Object.entries(trees)) {
			for (const root of roots) walk(root, expanded[Number(sessionId)]);
		}
        if (wanted.size === 0) return;

        let cancelled = false;
        void host.records.getMetadata([...wanted]).then((loaded) => {
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
    }, [expanded, trees, host]);

	const select = useCallback((sessionId: number, node: DownloadSessionUrlNode) => {
        selectedSessionIdRef.current = null;
        setSelectedSessionId(null);
		const selection = {sessionId, urlId: node.id};
		selectedUrlRef.current = selection;
		setSelectedUrl(selection);
        showRecords(subtreeRecords(node), `download: ${shortUrl(node.url)}`);
    }, [showRecords]);

	const selectFlat = useCallback((sessionId: number, entry: DownloadSessionUrlFlat) => {
        selectedSessionIdRef.current = null;
        setSelectedSessionId(null);
		const selection = {sessionId, urlId: entry.id};
		selectedUrlRef.current = selection;
		setSelectedUrl(selection);
        showRecords(entry.record_ids, `download: ${shortUrl(entry.url)}`);
    }, [showRecords]);

    const selectSession = useCallback(async (session: DownloadSessionInfo) => {
		selectedUrlRef.current = null;
		setSelectedUrl(null);
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
            <ImportStorageNotice storage={storage}/>
            <form onSubmit={event => {
                event.preventDefault();
                void queue();
            }}>
            <div className="download-session-row">
        <textarea
            value={draft}
            onChange={(event) => setDraft(event.target.value)}
            placeholder="Paste one URL per line"
            disabled={busy || !canImport}
        />
                <button type="submit" disabled={busy || !canImport || parseNewlineUrlList(draft).length === 0}>Download
                    URL(s)
                </button>
            </div>
                <DownloadOptions settings={downloadSettings} onChange={setDownloadSettings}
                                 disabled={busy || !canImport}/>
            </form>
			<ParserInfo/>
			<DownloadRateLimits/>
			<DownloadParserCache/>
            <div className="download-tree-toolbar">
                <label>
                    <input type="checkbox" checked={flatten} onChange={(event) => setFlatten(event.target.checked)}/>
                    Flat list
                </label>
                <span className={live ? 'download-tree-live' : 'muted'}>{live ? 'Live' : 'Reconnecting'}</span>
				<span aria-label="Global download speed">Download: {formatNetworkSpeed(downloadSpeed)}</span>
                <span
                    className="download-tree-tally">{importOutcomes.processed} Processed, {importOutcomes.newlyImported} New</span>
                {summary.map(([state, count]) => (
                    <span key={state} className={`download-tree-tally state-${state}`}>
                        {count} {stateLabel(state as DownloadDisplayState)}
                    </span>
                ))}
            </div>

            {streamError && <p className="error" role="alert">{streamError}</p>}
            {error && <p className="error" role="alert">{error}</p>}
            <div className="download-tree-scroll">
                {sessions.length === 0 && <p className="muted">No downloads yet.</p>}

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
                        {session.downloading !== undefined && <p className="muted" aria-label="Session network stats">
                            Downloading {session.downloading} ·
                            Waiting: {session.waiting_parsers ?? 0} Parser, {session.waiting_requests ?? 0} Requests
							{' · '}Download: {formatNetworkSpeed(sessionDownloadSpeeds[session.id] ?? 0)}
                            {' · '}Downloaded: {formatDownloadBytes(session.downloaded_bytes ?? 0)}
                            {' · '}Avg/import: {formatDownloadBytes(session.average_import_bytes ?? 0)}
                        </p>}
                        <DownloadSessionSummary nodes={trees[session.id]}/>
                        <ErrorLog
                            count={session.error_count}
							failures={failures[session.id]}
                            log={errorLogs[session.id]}
                            loadError={errorLogFailures[session.id]}
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
											className={`download-tree-url${selectedUrl?.sessionId === session.id && selectedUrl.urlId === entry.id ? ' is-selected' : ''}`}
                                            title={entry.url}
                                            onClick={(event) => {
                                                if (event.ctrlKey) {
                                                    openDownloadUrl(entry.url);
                                                    return;
                                                }
												selectFlat(session.id, entry);
                                            }}
                                            onDoubleClick={() => openDownloadUrl(entry.url)}
                                        >
                                            <span className="download-tree-url-text">{shortUrl(entry.url)}</span>
                                        </button>
                                        <span className="download-tree-state">
                                            {rowStatusLabel(flatDisplayState(entry), entry.stage)}
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
                                                    </span>
                                                    {child.record_id !== null && <span
                                                        className="download-tree-record">#{child.record_id}</span>}
                                                    <span
                                                        className="download-tree-state">{rowStatusLabel(child.state, child.stage)}</span>
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
										selectedId={selectedUrl?.sessionId === session.id ? selectedUrl.urlId : null}
										expanded={expanded[session.id]}
                                        info={info}
                                        thumbnailUrl={(id, size) => host.records.thumbnailUrl(id, size)}
										onToggle={(id) => toggle(session.id, id)}
										onSelect={(node) => select(session.id, node)}
                                        onRetry={(id) => void retry(session.id, id)}
                                        retryDisabled={!canImport}
                                        onOpenMedia={openMedia}
                                        onRecordMenu={(event, recordId) => openRecordMenu(event, [recordId])}
                                    />
                                ))}
                            </ul>
                        )}
                    </section>
                ))}
            </div>

            {recordMenu}
        </div>
    );
}

export const downloadsPanel = {
    type: 'downloads',
    title: 'Downloads',
	description: 'Download sessions.',
    component: DownloadsPanel,
    configVersion: 1,
} as const;
