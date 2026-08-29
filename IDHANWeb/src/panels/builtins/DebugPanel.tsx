/**
 * Debug: developer diagnostics. The Sort Profiler runs a blank search, so the full ordered id set,
 * once per sort type and direction, one after another, and times each.
 *
 * It goes through `host.http.fetch` rather than `host.search.run`, because the shared client returns
 * only the parsed SearchResponse and hides the client-measured round-trip time and raw payload size.
 */

import {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import type {PanelProps} from '../../host/types';
import {api, watchRateLimits} from '../../api/client';
import type {
    DebugEventKind,
    DebugLane,
    DebugSession,
    DebugSessionEvent,
    RateLimitLane,
    SearchResponse,
    SortOrder,
} from '../../api/types';
import {formatBytes} from './RecordInfoView';
import {SORT_OPTIONS} from './SearchPanel';

type SortValue = (typeof SORT_OPTIONS)[number]['value'];
const ORDERS: SortOrder[] = ['desc', 'asc'];

interface SortJob {
    value: SortValue;
    label: string;
    order: SortOrder;
}

/** Every sort type by direction combination, in a fixed run order. */
export function buildSortJobs(options: readonly { value: SortValue; label: string }[] = SORT_OPTIONS): SortJob[] {
    return options.flatMap((opt) => ORDERS.map((order) => ({value: opt.value, label: opt.label, order})));
}

export interface SortProfileRow extends SortJob {
    /** Server-reported SearchResponse.query_ms, the sort query cost being profiled. */
    queryMs: number;
    /** Client wall time from just before fetch to just after response.text() resolves. */
    roundTripMs: number;
    /** Decoded response body size in bytes, separating a slow query from a large payload. */
    bytes: number;
    count: number;
    truncated: boolean;
}

/** Rows sorted slowest-first by server query time. */
export function sortRowsBySlowest(rows: readonly SortProfileRow[]): SortProfileRow[] {
    return [...rows].sort((a, b) => b.queryMs - a.queryMs);
}

export interface RateLimitDisplay extends RateLimitLane {
    expiresAt: number;
}

export function rateLimitDisplay(limit: RateLimitLane, now = performance.now()): RateLimitDisplay {
    return {...limit, expiresAt: now + (limit.active ? limit.remaining_ms : 0)};
}

export function mergeRateLimits(
    current: readonly RateLimitDisplay[],
    incoming: readonly RateLimitLane[],
    now = performance.now(),
    replace = false,
): RateLimitDisplay[] {
    const limits = new Map<string, RateLimitDisplay>();
    if (!replace) current.forEach((limit) => limits.set(limit.scheduling_key, limit));
    incoming.forEach((limit) => limits.set(limit.scheduling_key, rateLimitDisplay(limit, now)));
    return [...limits.values()].sort((a, b) => a.scheduling_key.localeCompare(b.scheduling_key));
}

export function remainingRateLimitMs(limit: RateLimitDisplay, now = performance.now()): number {
    return limit.active ? Math.max(0, limit.expiresAt - now) : 0;
}

export function isRateLimitActive(limit: RateLimitDisplay, now = performance.now()): boolean {
    return remainingRateLimitMs(limit, now) > 0;
}

export function formatRateLimitRate(limit: Pick<RateLimitLane, 'throttled' | 'requests' | 'seconds'>): string {
    if (!limit.throttled) return 'Unlimited';
    return `${limit.requests} request${limit.requests === 1 ? '' : 's'} / ${limit.seconds} second${limit.seconds === 1 ? '' : 's'}`;
}

export function formatEffectiveRateLimitRate(effectiveIntervalMs: number): string {
    const seconds = effectiveIntervalMs / 1000;
    const display = Number.isInteger(seconds) ? String(seconds) : seconds.toFixed(3).replace(/0+$/, '').replace(/\.$/, '');
    return `1 request / ${display} second${seconds === 1 ? '' : 's'}`;
}

export function formatRateLimitRemaining(remainingMs: number): string {
    if (remainingMs <= 0) return '—';
    return `${(remainingMs / 1000).toFixed(1)} s`;
}

function RateLimits({host}: PanelProps) {
    const [limits, setLimits] = useState<RateLimitDisplay[]>([]);
    const [now, setNow] = useState(() => performance.now());

    useEffect(() => {
        const timer = window.setInterval(() => setNow(performance.now()), 250);
        return () => window.clearInterval(timer);
    }, []);

    useEffect(() => watchRateLimits({
        onSnapshot: (snapshot) => setLimits((previous) => mergeRateLimits(previous, snapshot, performance.now(), true)),
        onUpdate: (limit) => setLimits((previous) => mergeRateLimits(previous, [limit])),
        onError: (message) => host.ui.toast(message, {kind: 'error'}),
    }), [host]);

    return (
        <div className="debug-tool">
            {limits.length === 0 ? (
                <p className="muted">No downloader request lanes have been observed yet.</p>
            ) : (
                <div className="debug-rate-table">
                    <div className="debug-rate-row debug-rate-head muted">
                        <span>Lane</span>
                        <span>Configured rate</span>
                        <span>Real rate</span>
                        <span>State</span>
                        <span>Next request</span>
                    </div>
                    {limits.map((limit) => {
                        const remaining = remainingRateLimitMs(limit, now);
                        const active = isRateLimitActive(limit, now);
                        return (
                            <div key={limit.scheduling_key} className="debug-rate-row">
                                <span className="debug-rate-lane"
                                      title={limit.scheduling_key}>{limit.scheduling_key}</span>
                                <span>{formatRateLimitRate(limit)}</span>
                                <span
                                    title={limit.consecutive_limits > 0 ? `${limit.consecutive_limits} consecutive rate-limit response${limit.consecutive_limits === 1 ? '' : 's'}` : undefined}>
                                    {limit.throttled ? formatEffectiveRateLimitRate(limit.effective_interval_ms) : 'Unlimited'}
                                    {limit.consecutive_limits > 0 ? ` · ${limit.consecutive_limits} backoff${limit.consecutive_limits === 1 ? '' : 's'}` : ''}
                                </span>
                                <span
                                    className={active ? 'debug-rate-active' : 'debug-rate-inactive'}>{active ? 'Active' : 'Inactive'}</span>
                                <span className="debug-num">{formatRateLimitRemaining(remaining)}</span>
                            </div>
                        );
                    })}
                </div>
            )}
        </div>
    );
}

export const EVENT_BUFFER_LIMIT = 500;

export interface SessionEventBuffer {
    events: DebugSessionEvent[];
    cursor: number;
    dropped: number;
}

export function emptyEventBuffer(): SessionEventBuffer {
    return {events: [], cursor: 0, dropped: 0};
}

/** Deduplicates overlapping polls by sequence before enforcing the buffer limit. */
export function appendEvents(
    buffer: SessionEventBuffer,
    incoming: readonly DebugSessionEvent[],
    dropped: number,
    limit = EVENT_BUFFER_LIMIT,
): SessionEventBuffer {
    const fresh = incoming.filter((event) => event.sequence > buffer.cursor);
    if (fresh.length === 0 && dropped === 0) return buffer;

    const events = [...buffer.events, ...fresh];
    const cursor = fresh.reduce((highest, event) => Math.max(highest, event.sequence), buffer.cursor);
    return {
        events: events.length > limit ? events.slice(events.length - limit) : events,
        cursor,
        dropped: buffer.dropped + dropped,
    };
}

export function mergeEventBuffers(
    current: ReadonlyMap<number, SessionEventBuffer>,
    sessions: readonly DebugSession[],
    limit = EVENT_BUFFER_LIMIT,
): Map<number, SessionEventBuffer> {
    const merged = new Map<number, SessionEventBuffer>();
    sessions.forEach((session) => {
        const buffer = current.get(session.id) ?? emptyEventBuffer();
        merged.set(session.id, appendEvents(buffer, session.events, session.events_dropped, limit));
    });
    return merged;
}

export function cursorsFrom(buffers: ReadonlyMap<number, SessionEventBuffer>): Map<number, number> {
    const cursors = new Map<number, number>();
    buffers.forEach((buffer, id) => {
        if (buffer.cursor > 0) cursors.set(id, buffer.cursor);
    });
    return cursors;
}

/** Advances server-reported ages locally between polls. */
export function projectedAge(ageMs: number, receivedAt: number, now = performance.now()): number {
    return ageMs + Math.max(0, now - receivedAt);
}

export function activeLanes(lanes: readonly DebugLane[]): DebugLane[] {
    return lanes.filter((lane) => lane.active);
}

export function sortLanesByLoad(lanes: readonly DebugLane[]): DebugLane[] {
    return [...lanes].sort((a, b) => {
        const load = (b.queued + b.in_flight) - (a.queued + a.in_flight);
        return load !== 0 ? load : a.scheduling_key.localeCompare(b.scheduling_key);
    });
}

export function formatLaneInterval(lane: Pick<DebugLane, 'throttled' | 'effective_interval_ms'>): string {
    if (!lane.throttled) return 'Unlimited';

    const seconds = lane.effective_interval_ms / 1000;
    const display = Number.isInteger(seconds) ? String(seconds) : seconds.toFixed(2).replace(/0+$/, '').replace(/\.$/, '');
    return `1 / ${display}s`;
}

export function formatAge(ageMs: number): string {
    if (ageMs < 1000) return `${ageMs} ms`;
    if (ageMs < 60000) return `${(ageMs / 1000).toFixed(1)} s`;
    const minutes = Math.floor(ageMs / 60000);
    const seconds = Math.round((ageMs % 60000) / 1000);
    return `${minutes}m ${seconds}s`;
}

export function formatEventTime(atMicros: number): string {
    const date = new Date(atMicros / 1000);
    const time = date.toLocaleTimeString(undefined, {hour12: false});
    return `${time}.${String(date.getMilliseconds()).padStart(3, '0')}`;
}

export function describeEvent(event: DebugSessionEvent): string {
    switch (event.kind) {
        case 'request':
            return `${event.status} ${formatBytes(event.bytes)} ${event.detail} ${event.url}`.trim();
        case 'imported':
            return `record ${event.record_id ?? '?'} ${formatBytes(event.bytes)} ${event.detail} ${event.url}`.trim();
        case 'followed':
            return `${event.detail} ${event.url}`.trim();
        case 'failed':
        case 'import_failed':
            return `${event.url} ${event.detail}`.trim();
        case 'finished':
            return 'session drained';
        default:
            return event.url;
    }
}

const EVENT_TONE: Record<DebugEventKind, string> = {
    started: 'debug-event-neutral',
    completed: 'debug-event-good',
    failed: 'debug-event-bad',
    request: 'debug-event-neutral',
    imported: 'debug-event-good',
    import_failed: 'debug-event-bad',
    followed: 'debug-event-muted',
    finished: 'debug-event-muted',
};

const COUNTER_ROWS: readonly (readonly [label: string, key: keyof DebugSession['counters'], bytes?: boolean])[] = [
    ['Scripts run', 'work_started'],
    ['Completed', 'work_completed'],
    ['Failed', 'work_failed'],
    ['Requests', 'requests'],
    ['Downloaded', 'request_bytes', true],
    ['Imported', 'imported'],
    ['Import bytes', 'import_bytes', true],
    ['Import failed', 'import_failed'],
    ['Follows queued', 'follows_queued'],
    ['Filtered', 'follows_filtered'],
    ['Already queued', 'follows_already_queued'],
    ['Already explored', 'follows_already_explored'],
    ['Already imported', 'follows_already_imported'],
];

function SessionCard(
    {session, buffer, receivedAt, now}:
    { session: DebugSession; buffer: SessionEventBuffer; receivedAt: number; now: number },
) {
    const [open, setOpen] = useState(true);

    return (
        <div className="debug-session">
            <button type="button" className="debug-session-head" onClick={() => setOpen((was) => !was)}>
                <span className="debug-session-caret">{open ? '\u25be' : '\u25b8'}</span>
                <span className="debug-session-name">{session.name ?? `Session ${session.id}`}</span>
                <span className={`debug-session-state debug-state-${session.state}`}>{session.state}</span>
                <span className="muted debug-session-url" title={session.root_url}>{session.root_url}</span>
                <span className="debug-num">{session.outstanding.toLocaleString()} outstanding</span>
            </button>

            {open && (
                <div className="debug-session-body">
                    <div className="debug-session-stats">
                        <span>Queued <b className="debug-num">{session.queued.toLocaleString()}</b></span>
                        <span>Running <b className="debug-num">{session.running.toLocaleString()}</b></span>
                        <span>
                            In flight
                            <b className="debug-num">{session.in_flight}/{session.in_flight_limit}</b>
                        </span>
                    </div>

                    <div className="debug-counter-grid">
                        {COUNTER_ROWS.map(([label, key, bytes]) => (
                            <span key={key} className="debug-counter">
                                <span className="muted">{label}</span>
                                <b className="debug-num">
                                    {bytes ? formatBytes(session.counters[key]) : session.counters[key].toLocaleString()}
                                </b>
                            </span>
                        ))}
                    </div>

                    <h4 className="debug-subsection muted">Running scripts</h4>
                    {session.work.length === 0 ? (
                        <p className="muted">No scripts are holding a realm right now.</p>
                    ) : (
                        <table className="debug-table debug-work-table">
                            <thead>
                            <tr>
                                <th scope="col">Work</th>
                                <th scope="col">Class</th>
                                <th scope="col">URL</th>
                                <th scope="col">Phase</th>
                                <th scope="col" className="debug-col-num"
                                    title="Requests still to settle into this realm">
                                    Reqs
                                </th>
                                <th scope="col" className="debug-col-num">Age</th>
                            </tr>
                            </thead>
                            <tbody>
                            {session.work.map((work) => (
                                <tr key={work.work_id}>
                                    <td className="debug-num">#{work.work_id}</td>
                                    <td title={`${work.url_class} \u00b7 ${work.parser}`}>
                                        {work.url_class || 'unrouted'}
                                    </td>
                                    <td className="debug-ellipsis" title={work.url}>{work.url}</td>
                                    <td className="muted">{work.phase}</td>
                                    <td className="debug-num">{work.outstanding}</td>
                                    <td className="debug-num">
                                        {formatAge(projectedAge(work.age_ms, receivedAt, now))}
                                    </td>
                                </tr>
                            ))}
                            </tbody>
                        </table>
                    )}

                    <h4 className="debug-subsection muted">Pending requests</h4>
                    {session.requests.length === 0 ? (
                        <p className="muted">Nothing is waiting on the network.</p>
                    ) : (
                        <table className="debug-table debug-pending-table">
                            <thead>
                            <tr>
                                <th scope="col">Work</th>
                                <th scope="col">Lane</th>
                                <th scope="col">URL</th>
                                <th scope="col">Kind</th>
                                <th scope="col" className="debug-col-num">Age</th>
                            </tr>
                            </thead>
                            <tbody>
                            {session.requests.map((pending, index) => (
                                <tr key={`${pending.work_id}:${pending.url}:${index}`}>
                                    <td className="debug-num">#{pending.work_id}</td>
                                    <td className="debug-ellipsis" title={pending.lane}>
                                        {pending.lane || '\u2014'}
                                    </td>
                                    <td className="debug-ellipsis" title={pending.url}>{pending.url}</td>
                                    <td className="muted">{pending.import ? 'import' : 'request'}</td>
                                    <td className="debug-num">
                                        {formatAge(projectedAge(pending.age_ms, receivedAt, now))}
                                    </td>
                                </tr>
                            ))}
                            </tbody>
                        </table>
                    )}

                    <h4 className="debug-subsection muted">
                        Events
                        {buffer.dropped > 0 ? ` \u00b7 ${buffer.dropped.toLocaleString()} missed` : ''}
                    </h4>
                    {buffer.events.length === 0 ? (
                        <p className="muted">No pipeline events have been observed yet.</p>
                    ) : (
                        <div className="debug-event-log">
                            {[...buffer.events].reverse().map((event) => (
                                <div key={event.sequence} className={`debug-event ${EVENT_TONE[event.kind]}`}>
                                    <span className="debug-num">{formatEventTime(event.at)}</span>
                                    <span className="debug-event-kind">{event.kind}</span>
                                    <span className="debug-num">{event.work_id === 0 ? '' : `#${event.work_id}`}</span>
                                    <span className="debug-ellipsis" title={describeEvent(event)}>
                                        {describeEvent(event)}
                                    </span>
                                </div>
                            ))}
                        </div>
                    )}
                </div>
            )}
        </div>
    );
}

function Lanes({lanes, receivedAt, now}: { lanes: readonly DebugLane[]; receivedAt: number; now: number }) {
    const rows = useMemo(() => sortLanesByLoad(activeLanes(lanes)), [lanes]);

    if (rows.length === 0) return <p className="muted">No lanes are holding connections right now.</p>;

    return (
        <table className="debug-table debug-lane-table">
            <thead>
            <tr>
                <th scope="col">Lane</th>
                <th scope="col">Rate</th>
                <th scope="col" className="debug-col-num" title="Transfers waiting for a slot on this lane">
                    Queued
                </th>
                <th scope="col" className="debug-col-num" title="Transfers this lane has running">
                    In flight
                </th>
                <th scope="col" className="debug-col-num" title="Connection pools backing this lane">
                    Shards
                </th>
                <th scope="col" className="debug-col-num" title="Time until this lane may dispatch again">
                    Next slot
                </th>
            </tr>
            </thead>
            <tbody>
            {rows.map((lane) => {
                const remaining = Math.max(0, lane.remaining_ms - Math.max(0, now - receivedAt));

                return (
                    <tr key={lane.scheduling_key}>
                        <td className="debug-ellipsis"
                            title={lane.group ? `${lane.scheduling_key} (group: ${lane.group})` : lane.scheduling_key}>
                            {lane.scheduling_key}
                        </td>
                        <td className="muted"
                            title={lane.throttled ? formatEffectiveRateLimitRate(lane.effective_interval_ms) : 'Unlimited'}>
                            {formatLaneInterval(lane)}
                        </td>
                        <td className="debug-num">{lane.queued.toLocaleString()}</td>
                        <td className="debug-num">{lane.in_flight}</td>
                        <td className="debug-num">{lane.shards}</td>
                        <td className={`debug-num ${lane.backed_off ? 'debug-lane-backoff' : ''}`}
                            title={lane.backed_off ? `Backed off after ${lane.consecutive_limits} failures` : undefined}>
                            {formatRateLimitRemaining(remaining)}
                        </td>
                    </tr>
                );
            })}
            </tbody>
        </table>
    );
}

function DownloadSessions({host}: PanelProps) {
    const [lanes, setLanes] = useState<DebugLane[]>([]);
    const [sessions, setSessions] = useState<DebugSession[]>([]);
    const [receivedAt, setReceivedAt] = useState(() => performance.now());
    const [now, setNow] = useState(() => performance.now());
    const [error, setError] = useState<string | null>(null);
    const buffers = useRef<Map<number, SessionEventBuffer>>(new Map());

    useEffect(() => {
        const timer = window.setInterval(() => setNow(performance.now()), 250);
        return () => window.clearInterval(timer);
    }, []);

    useEffect(() => {
        const controller = new AbortController();
        let timer: ReturnType<typeof setTimeout> | undefined;
        let stopped = false;

        const poll = async () => {
            try {
                const debug = await api.downloader.debug(cursorsFrom(buffers.current), controller.signal);
                if (stopped) return;
                buffers.current = mergeEventBuffers(buffers.current, debug.sessions);
                setSessions(debug.sessions);
                setLanes(debug.lanes);
                setReceivedAt(performance.now());
                setError(null);
            } catch (caught) {
                if (stopped || isAbort(caught)) return;
                setError(caught instanceof Error ? caught.message : String(caught));
            } finally {
                if (!stopped) timer = setTimeout(() => void poll(), 1000);
            }
        };

        void poll();

        return () => {
            stopped = true;
            controller.abort();
            if (timer !== undefined) clearTimeout(timer);
        };
    }, [host]);

    if (error !== null) return <p className="debug-tool muted">Could not read downloader state: {error}</p>;

    return (
        <div className="debug-tool">
            <h4 className="debug-subsection muted">Lanes</h4>
            <Lanes lanes={lanes} receivedAt={receivedAt} now={now}/>

            {sessions.length === 0 ? (
                <p className="muted">No download sessions are live. A session appears once a URL is submitted to
                    it.</p>
            ) : (
                sessions.map((session) => (
                    <SessionCard
                        key={session.id}
                        session={session}
                        buffer={buffers.current.get(session.id) ?? emptyEventBuffer()}
                        receivedAt={receivedAt}
                        now={now}
                    />
                ))
            )}
        </div>
    );
}

async function profileSort(host: PanelProps['host'], job: SortJob, signal: AbortSignal): Promise<SortProfileRow> {
    const body = JSON.stringify({sort: {by: job.value, order: job.order}});
    const start = performance.now();
    const response = await host.http.fetch('/search', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body,
        signal,
    });
    const text = await response.text();
    const roundTripMs = performance.now() - start;
    if (!response.ok) throw new Error(`/search → ${response.status}`);
    const parsed = JSON.parse(text) as SearchResponse;
    return {
        ...job,
        queryMs: parsed.query_ms,
        roundTripMs,
        bytes: new TextEncoder().encode(text).length,
        count: parsed.count,
        truncated: parsed.truncated,
    };
}

function isAbort(error: unknown): boolean {
    return error instanceof DOMException && error.name === 'AbortError';
}

function jobKey(job: SortJob): string {
    return `${job.value}:${job.order}`;
}

function SortProfiler({host}: PanelProps) {
    const [rows, setRows] = useState<SortProfileRow[]>([]);
    const [running, setRunning] = useState(false);
    const [progress, setProgress] = useState<string | null>(null);
    const abortRef = useRef<AbortController | null>(null);

    useEffect(() => () => abortRef.current?.abort(), []);

    const runAll = useCallback(async () => {
        const controller = new AbortController();
        abortRef.current = controller;
        setRunning(true);
        setRows([]);
        const jobs = buildSortJobs();
        try {
            for (let i = 0; i < jobs.length; i++) {
                const job = jobs[i]!;
                setProgress(`${job.label} · ${job.order} (${i + 1}/${jobs.length})`);
                const row = await profileSort(host, job, controller.signal);
                setRows((prev) => [...prev, row]);
            }
        } catch (error) {
            if (!isAbort(error)) {
                const message = error instanceof Error ? error.message : String(error);
                host.ui.toast(`Sort profiling failed: ${message}`, {kind: 'error'});
            }
        } finally {
            setRunning(false);
            setProgress(null);
            abortRef.current = null;
        }
    }, [host]);

    const stop = () => abortRef.current?.abort();

    const displayRows = useMemo(() => sortRowsBySlowest(rows), [rows]);
    const totalJobs = SORT_OPTIONS.length * ORDERS.length;

    return (
        <div className="debug-tool">
            <div className="debug-toolbar">
                <button type="button" className="toolbar-button" onClick={() => void runAll()} disabled={running}>
                    {running ? 'Running…' : 'Run all'}
                </button>
                <button type="button" className="toolbar-button" onClick={stop} disabled={!running}>
                    Stop
                </button>
                <span className="muted grow debug-progress">
          {running ? progress : rows.length > 0 ? `${rows.length}/${totalJobs} runs · sorted slowest first` : `${totalJobs} sort × direction combos`}
        </span>
            </div>

            {displayRows.length === 0 ? (
                <p className="muted">Runs a blank search (no tags) once per sort type and direction, one after another,
                    and times each.</p>
            ) : (
                <div className="debug-sort-table">
                    <div className="debug-sort-row debug-sort-head muted">
                        <span>Sort</span>
                        <span>Order</span>
                        <span>Query ms</span>
                        <span>Round trip</span>
                        <span>Overhead</span>
                        <span>Size</span>
                        <span>Results</span>
                    </div>
                    {displayRows.map((row) => (
                        <div key={jobKey(row)} className="debug-sort-row">
                            <span title={row.value}>{row.label}</span>
                            <span className="muted">{row.order}</span>
                            <span className="debug-num">{row.queryMs.toFixed(0)}</span>
                            <span className="debug-num">{row.roundTripMs.toFixed(1)}</span>
                            <span className="debug-num muted">{(row.roundTripMs - row.queryMs).toFixed(1)}</span>
                            <span className="debug-num">{formatBytes(row.bytes)}</span>
                            <span className="debug-num">
                {row.count.toLocaleString()}
                                {row.truncated ? '†' : ''}
              </span>
                        </div>
                    ))}
                </div>
            )}
        </div>
    );
}

function DebugPanel({host}: PanelProps) {
    return (
        <div className="panel-body debug-panel">
            <h3 className="debug-section-title">Download Sessions</h3>
            <DownloadSessions host={host}/>
            <h3 className="debug-section-title">Rate Limits</h3>
            <RateLimits host={host}/>
            <h3 className="debug-section-title">Sort Profiler</h3>
            <SortProfiler host={host}/>
        </div>
    );
}

export const debugPanel = {
    type: 'debug',
    title: 'Debug',
    description: 'Developer diagnostics: live download sessions, throttle lanes, and search sort cost.',
    component: DebugPanel,
    singleton: true,
} as const;
