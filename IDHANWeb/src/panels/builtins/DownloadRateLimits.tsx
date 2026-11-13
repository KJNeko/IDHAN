import {useEffect, useState} from 'react';
import {api} from '../../api/client';
import type {RateLimitLane} from '../../api/types';

function laneStatus(lane: RateLimitLane): string {
	if (lane.disabled) return 'Disabled after repeated HTTP 429';

	if (lane.parser_limited) return 'Parser error cooldown';

	if (lane.rate_limited) return 'HTTP 429 cooldown';

	if (lane.backed_off) return 'Cooldown';

	if (lane.dispatch_wait === 'rate_limit') return 'Waiting for rate allowance';

	if (lane.dispatch_wait === 'concurrency') return 'Waiting for active requests';

	if ((lane.queued ?? 0) > 0) return 'Pending';

	return (lane.in_flight ?? 0) > 0 ? 'Downloading' : 'Idle';
}

export function DownloadRateLimits() {
	const [lanes, setLanes] = useState<RateLimitLane[]>();
	const [error, setError] = useState('');
	const [actionError, setActionError] = useState('');
	const [enabling, setEnabling] = useState<string | null>(null);
	useEffect(() => {
		const controller = new AbortController();
		let timer: ReturnType<typeof setTimeout>;

		async function refresh() {
			try {
				const next = await api.downloader.rateLimits(controller.signal);
				if (controller.signal.aborted) return;

				setLanes(next);
				setError('');
			} catch (failure) {
				if (controller.signal.aborted) return;

				setError(`Could not refresh rate limits: ${failure instanceof Error ? failure.message : String(failure)}`);
			}
			if (!controller.signal.aborted) timer = setTimeout(refresh, 1000);
		}

		void refresh();
		return () => {
			controller.abort();
			clearTimeout(timer);
		};
	}, []);

	async function enable(lane: string) {
		setEnabling(lane);
		setActionError('');
		try {
			await api.downloader.enableLane(lane);
			setLanes(await api.downloader.rateLimits());
		} catch (failure) {
			setActionError(`Could not enable lane: ${failure instanceof Error ? failure.message : String(failure)}`);
		} finally {
			setEnabling(null);
		}
	}

	return <section className="download-rate-limits" aria-label="Download rate limits">
		<strong>Rate limits (all sessions)</strong>
		{actionError && <p className="error" role="alert">{actionError}</p>}
		{error &&
            <p className="error" role="alert">{error}{lanes !== undefined && ' — showing last received values.'}</p>}
		{lanes === undefined && !error && <p className="muted">Loading rate limits…</p>}
		{lanes?.length === 0 && <p className="muted">No host requests yet.</p>}
		{lanes !== undefined && lanes.length > 0 && <div className="download-rate-limits-scroll">
            <table>
                <thead>
                <tr>
                    <th scope="col">Host Lane</th>
                    <th scope="col">Request allowance</th>
                    <th scope="col">State</th>
                    <th scope="col">Wait</th>
                    <th scope="col">Pending</th>
                    <th scope="col">Active</th>
                    <th scope="col">429 responses</th>
                    <th scope="col">Parser errors</th>
                    <th scope="col">Site limit headers</th>
                    <th scope="col">Action</th>
                </tr>
                </thead>
                <tbody>{[...lanes].sort((a, b) => Number(Boolean(b.disabled || b.rate_limited || b.parser_limited)) - Number(Boolean(a.disabled || a.rate_limited || a.parser_limited))
					|| (b.queued ?? 0) - (a.queued ?? 0) || a.lane.localeCompare(b.lane)).map(lane => <tr
					key={lane.lane}>
					<th scope="row">{lane.lane}</th>
					<td>{lane.requests > 0 ? `${lane.requests} / ${lane.seconds}s${lane.spread ? ' (spread)' : ' (burst)'}` : 'Unlimited'}</td>
					<td className={lane.disabled || lane.rate_limited || lane.parser_limited ? 'error' : undefined}>{laneStatus(lane)}</td>
					<td>{lane.remaining_ms > 0 ? `${Math.ceil(lane.remaining_ms / 1000)}s` : '—'}</td>
					<td>{lane.queued ?? 0}</td>
					<td>{lane.in_flight ?? 0} / {lane.concurrency_limit ?? '—'}</td>
					<td>{lane.rate_limit_hits ?? '—'}</td>
					<td>{lane.parser_error_hits ?? '—'}</td>
					<td>{lane.advertised_limits || 'Not supplied'}</td>
					<td>{lane.disabled && <button type="button" className="toolbar-button"
                                                  aria-label={`Enable lane ${lane.lane}`} disabled={enabling !== null}
                                                  onClick={() => void enable(lane.lane)}>{enabling === lane.lane ? 'Enabling…' : 'Enable lane'}</button>}</td>
				</tr>)}</tbody>
            </table>
        </div>}
	</section>;
}
