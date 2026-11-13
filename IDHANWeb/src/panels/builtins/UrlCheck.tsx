import {useState} from 'react';
import {api} from '../../api/client';
import type {UrlCheckResult} from '../../api/types';
import type {PanelProps} from '../../host/types';

export function UrlCheck({onShowRecords, expanded = false}: {
	onShowRecords: (ids: number[], label: string) => void;
	expanded?: boolean;
}) {
	const [url, setUrl] = useState('');
	const [busy, setBusy] = useState(false);
	const [result, setResult] = useState<UrlCheckResult>();
	const [error, setError] = useState<string>();

	async function check() {
		if (busy || !url.trim()) return;
		setBusy(true);
		setResult(undefined);
		setError(undefined);
		try {
			setResult(await api.downloadSessions.checkUrl(url.trim()));
		} catch (caught) {
			setError(caught instanceof Error ? caught.message : String(caught));
		} finally {
			setBusy(false);
		}
	}

	return <details className="download-options" open={expanded || undefined}>
		<summary>Check URL</summary>
		<p className="muted">Look up existing records and the saved direct URL classification.</p>
		<form className="download-session-row" onSubmit={event => {
			event.preventDefault();
			void check();
		}}>
			<label>URL to check <input type="url" required maxLength={8192} placeholder="https://example.com/image.jpg"
									   value={url} disabled={busy} onChange={event => {
				setUrl(event.target.value);
				setResult(undefined);
				setError(undefined);
			}}/></label>
			<button type="submit" disabled={busy || !url.trim()}>{busy ? 'Checking…' : 'Check URL'}</button>
		</form>
		{busy && <p role="status">Looking up URL…</p>}
		{error && <p role="alert">{error}</p>}
		{result && <div role="status">
            <p>Normalized URL: <span className="url-check-normalized">{result.normalized_url}</span></p>
            <p>Known direct URL: {result.known_direct ? 'Yes' : 'No'}</p>
			{result.record_ids.length === 0 ? <p>No associated records.</p> : <>
				<p>{result.record_ids.length} associated
					record(s): {result.record_ids.map(id => `#${id}`).join(', ')}</p>
				<button type="button" onClick={() => onShowRecords(result.record_ids, `URL: ${result.normalized_url}`)}>
					Show records
				</button>
			</>}
        </div>}
	</details>;
}

function UrlCheckPanel({host}: PanelProps) {
	function showRecords(ids: number[], label: string) {
		host.results.set({ids: Int32Array.from(ids), queryMs: 0, query: [label]});
		host.selection.set([]);
	}

	return <div className="downloads-panel"><UrlCheck expanded onShowRecords={showRecords}/></div>;
}

export const urlCheckPanel = {
	type: 'url-check',
	title: 'URL Check',
	description: 'Look up records associated with a URL and its saved direct URL classification.',
	component: UrlCheckPanel,
	configVersion: 1,
} as const;
