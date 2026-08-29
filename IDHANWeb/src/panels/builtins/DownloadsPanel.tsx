import {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import {api, watchDownloadSessions} from '../../api/client';
import type {
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

function shortUrl(url: string): string {
    try {
        const parsed = new URL(url);
        return `${parsed.host}${parsed.pathname}${parsed.search}`;
    } catch {
        return url;
    }
}

export function parseNewlineUrlList(value: string): string[] {
    return value
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => line.length > 0);
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
                    onClick={() => onSelect(node)}
                >
                    {shortUrl(node.url)}
                </button>
                {node.record_id !== null && <span className="download-tree-record">#{node.record_id}</span>}
                <span className="download-tree-state">{stateLabel(state)}</span>
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
            {node.note && (
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
            {open && node.children.map((child) => (
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
        return [...counts.entries()].sort(([a], [b]) => a.localeCompare(b));
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
                        {flatten ? (
                            <ul className="download-tree">
                                {(flat[session.id] ?? []).map((entry) => (
                                    <li key={entry.id} className="download-tree-flat">
                                        <button
                                            type="button"
                                            className={`download-tree-url${selectedId === entry.id ? ' is-selected' : ''}`}
                                            title={entry.url}
                                            onClick={() => selectFlat(entry)}
                                        >
                                            {shortUrl(entry.url)}
                                        </button>
                                        <span className="download-tree-state">
                                            {stateLabel(flatDisplayState(entry))}
                                        </span>
                                        <p className="muted">
                                            {entry.urls.length} discovered, {entry.record_ids.length} imported
                                        </p>
                                        <ul className="download-tree-flat-urls">
                                            {entry.urls.map((child: DownloadSessionUrlJob) => (
                                                <li key={child.id} className={`state-${child.state}`}>
                                                    <span className="grow"
                                                          title={child.url}>{shortUrl(child.url)}</span>
                                                    {child.record_id !== null && <span
                                                        className="download-tree-record">#{child.record_id}</span>}
                                                    <span className="download-tree-state">{child.state}</span>
                                                </li>
                                            ))}
                                        </ul>
                                    </li>
                                ))}
                            </ul>
                        ) : (
                            <ul className="download-tree">
                                {(trees[session.id] ?? []).map((root) => (
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
