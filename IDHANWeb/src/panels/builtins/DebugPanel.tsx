/**
 * Debug: developer diagnostics. The Sort Profiler runs a blank search, so the full ordered id set,
 * once per sort type and direction, one after another, and times each.
 *
 * It goes through `host.http.fetch` rather than `host.search.run`, because the shared client returns
 * only the parsed SearchResponse and hides the client-measured round-trip time and raw payload size.
 */

import {useCallback, useEffect, useMemo, useRef, useState} from 'react';
import type {PanelProps} from '../../host/types';
import type {SearchResponse, SortOrder} from '../../api/types';
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
            <h3 className="debug-section-title">Sort Profiler</h3>
            <SortProfiler host={host}/>
        </div>
    );
}

export const debugPanel = {
    type: 'debug',
    title: 'Debug',
    description: 'Developer diagnostics: profile query cost per search sort type.',
    component: DebugPanel,
    singleton: true,
} as const;
