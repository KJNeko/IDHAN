import {useEffect, useState} from 'react';
import {api} from '../../api/client';
import type {ParserCacheSnapshot} from '../../api/types';

export function DownloadParserCache() {
	const [snapshot, setSnapshot] = useState<ParserCacheSnapshot>();
	const [error, setError] = useState('');
	useEffect(() => {
		const controller = new AbortController();
		let timer: ReturnType<typeof setTimeout>;

		async function refresh() {
			try {
				const next = await api.downloader.cache(controller.signal);
				if (controller.signal.aborted) return;

				setSnapshot(next);
				setError('');
			} catch (failure) {
				if (controller.signal.aborted) return;

				setError(`Could not refresh parser cache: ${failure instanceof Error ? failure.message : String(failure)}`);
			}
			if (!controller.signal.aborted) timer = setTimeout(refresh, 2000);
		}

		void refresh();
		return () => {
			controller.abort();
			clearTimeout(timer);
		};
	}, []);

	return <section className="download-rate-limits" aria-label="Parser KV cache">
		<strong title="Totals since downloader startup; the 100 cached keys with the most hits">Parser KV cache</strong>
		{error &&
            <p className="error" role="alert">{error}{snapshot !== undefined && ' — showing last received values.'}</p>}
		{snapshot === undefined && !error && <p className="muted">Loading parser cache…</p>}
		{snapshot !== undefined && <>
            <p>{snapshot.hits.toLocaleString()} hits · {snapshot.misses.toLocaleString()} misses
				{' · '}{(snapshot.hit_rate * 100).toFixed(1)}% hit rate
				{' · '}{snapshot.entry_count.toLocaleString()} cached {snapshot.entry_count === 1 ? 'key' : 'keys'}</p>
			{snapshot.entries.length === 0 ? <p className="muted">No cached keys.</p> :
				<div className="download-rate-limits-scroll">
					<table>
						<thead>
						<tr>
							<th scope="col">Parser</th>
							<th scope="col">Key</th>
							<th scope="col">Hits</th>
							<th scope="col">Lifetime</th>
							<th scope="col">Expires in</th>
						</tr>
						</thead>
						<tbody>{snapshot.entries.map(entry => <tr key={JSON.stringify([entry.parser, entry.key])}>
							<th scope="row">{entry.parser}</th>
							<td>{entry.key}</td>
							<td>{entry.hits.toLocaleString()}</td>
							<td>{entry.lifetime_seconds.toLocaleString()}s</td>
							<td>{entry.expires_in_seconds.toLocaleString()}s</td>
						</tr>)}</tbody>
					</table>
				</div>}
        </>}
	</section>;
}
