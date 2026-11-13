import {useEffect, useMemo, useState} from 'react';
import {readApiError} from '../../api/client';
import type {BandwidthNode} from '../../api/types';
import type {PanelProps} from '../../host/types';
import {formatBytes} from './RecordInfoView';

const PERIODS = [
	{value: '900', label: 'Last 15 minutes'},
	{value: '3600', label: 'Last hour'},
	{value: '86400', label: 'Last 24 hours'},
	{value: '604800', label: 'Last 7 days'},
	{value: '2592000', label: 'Last 30 days'},
	{value: 'all', label: 'All time'},
] as const;

type Period = (typeof PERIODS)[number]['value'];

export function bandwidthPath(period: Period, now = Date.now()): string {
	if (period === 'all') return '/bandwidth';
	// The API's upper bound is exclusive; include the current Unix second.
	const end = Math.floor(now / 1000) + 1;
	return `/bandwidth?start=${end - Number(period)}&end=${end}`;
}

interface HostRow {
	node: BandwidthNode;
	depth: number;
}

export function bandwidthRows(nodes: readonly BandwidthNode[], collapsed: ReadonlySet<string> = new Set()): HostRow[] {
	const rows: HostRow[] = [];

	function visit(level: readonly BandwidthNode[], depth: number) {
		for (const node of level) {
			rows.push({node, depth});
			if (!collapsed.has(node.host)) visit(node.children, depth + 1);
		}
	}

	visit(nodes, 0);
	return rows;
}

interface Snapshot {
	period: Period;
	nodes: BandwidthNode[];
	updatedAt: number;
}

export function BandwidthPanel({host}: PanelProps) {
	const [period, setPeriod] = useState<Period>('3600');
	const [auto, setAuto] = useState(true);
	const [refresh, setRefresh] = useState(0);
	const [snapshot, setSnapshot] = useState<Snapshot | null>(null);
	const [loading, setLoading] = useState(true);
	const [error, setError] = useState<string | null>(null);
	const [collapsed, setCollapsed] = useState<Set<string>>(new Set());

	useEffect(() => {
		const controller = new AbortController();
		let timer: number | undefined;

		async function load() {
			setLoading(true);
			setError(null);
			try {
				const response = await host.http.fetch(bandwidthPath(period), {signal: controller.signal});
				if (!response.ok) throw await readApiError(response, 'GET /bandwidth');
				const nodes = await response.json() as BandwidthNode[];
				if (!controller.signal.aborted) setSnapshot({period, nodes, updatedAt: Date.now()});
			} catch (reason) {
				if (!controller.signal.aborted) setError(reason instanceof Error ? reason.message : String(reason));
			} finally {
				if (!controller.signal.aborted) {
					setLoading(false);
					// Wait for the request to finish before scheduling another, even on a slow server.
					if (auto) timer = window.setTimeout(() => void load(), 10_000);
				}
			}
		}

		void load();
		return () => {
			controller.abort();
			window.clearTimeout(timer);
		};
	}, [host.http, period, auto, refresh]);

	const current = snapshot?.period === period ? snapshot : null;
	const allRows = useMemo(() => bandwidthRows(current?.nodes ?? []), [current]);
	const rows = useMemo(() => bandwidthRows(current?.nodes ?? [], collapsed), [current, collapsed]);
	const total = allRows.reduce((sum, {node}) => sum + node.bytes_received, 0);
	const hostsWithTraffic = allRows.filter(({node}) => node.bytes_received > 0).length;

	function toggleHost(hostname: string) {
		setCollapsed((previous) => {
			const next = new Set(previous);
			if (next.has(hostname)) next.delete(hostname);
			else next.add(hostname);
			return next;
		});
	}

	return <div className="panel-body bandwidth-panel">
		<div className="panel-row bandwidth-controls">
			<label className="bandwidth-period">
				Timeframe
				<select value={period} onChange={(event) => setPeriod(event.target.value as Period)}>
					{PERIODS.map((option) => <option key={option.value} value={option.value}>{option.label}</option>)}
				</select>
			</label>
			<button type="button" className="toolbar-button" disabled={loading}
			        onClick={() => setRefresh((value) => value + 1)}>
				{loading ? 'Refreshing…' : 'Refresh'}
			</button>
			<label className="panel-checkbox">
				<input type="checkbox" checked={auto} onChange={(event) => setAuto(event.target.checked)}/>
				Auto-refresh (10s)
			</label>
		</div>

		{error && <p className="error" role="alert">Could not load bandwidth: {error}</p>}
		{!current && loading && <p className="muted" role="status">Loading bandwidth…</p>}
		{current && <>
            <div className="bandwidth-summary">
                <div><strong className="bandwidth-total">{formatBytes(total)}</strong><span
                    className="muted"> received</span></div>
                <span
                    className="muted">{hostsWithTraffic.toLocaleString()} {hostsWithTraffic === 1 ? 'host' : 'hosts'} with traffic</span>
                <span className="muted">Updated {new Date(current.updatedAt).toLocaleTimeString()}</span>
            </div>
			{rows.length === 0 ? <p className="muted">No bandwidth recorded for this timeframe.</p> : <>
				<table className="bandwidth-table" aria-label="Received bandwidth by host" aria-busy={loading}>
					<thead>
					<tr>
						<th scope="col">Host</th>
						<th scope="col">Received</th>
					</tr>
					</thead>
					<tbody>{rows.map(({node, depth}) => <tr key={node.host}>
						<th scope="row">
							<div className="bandwidth-host" style={{paddingInlineStart: `${depth * 14}px`}}>
								{node.children.length > 0 ? <button type="button" className="bandwidth-toggle"
																	aria-expanded={!collapsed.has(node.host)}
																	aria-label={`${collapsed.has(node.host) ? 'Expand' : 'Collapse'} ${node.host}`}
																	onClick={() => toggleHost(node.host)}>{collapsed.has(node.host) ? '▸' : '▾'}</button>
									: <span className="bandwidth-toggle-space"/>}
								<span className="mono">{node.host}</span>
							</div>
						</th>
						<td title={`${node.bytes_received.toLocaleString()} bytes`}>{formatBytes(node.bytes_received)}</td>
					</tr>)}</tbody>
				</table>
			</>}
        </>}
	</div>;
}

export const bandwidthPanel = {
	type: 'bandwidth',
	title: 'Bandwidth',
	description: 'Received bytes by host.',
	component: BandwidthPanel,
	singleton: true,
} as const;
