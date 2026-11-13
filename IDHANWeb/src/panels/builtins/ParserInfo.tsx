import {useState} from 'react';
import {api} from '../../api/client';
import type {DownloaderParser} from '../../api/types';

export function ParserInfo() {
	const [file, setFile] = useState<File>();
	const [replace, setReplace] = useState(false);
	const [busy, setBusy] = useState(false);
	const [parsers, setParsers] = useState<DownloaderParser[]>();
	const [loading, setLoading] = useState(false);
	const [error, setError] = useState<string>();

	async function load() {
		setLoading(true);
		setError(undefined);
		try {
			setParsers((await api.downloadSessions.parsers()).parsers);
		} catch (caught) {
			setError(caught instanceof Error ? caught.message : String(caught));
		} finally {
			setLoading(false);
		}
	}

	async function upload() {
		if (!file) return;
		setBusy(true);
		setError(undefined);
		try {
			await api.downloadSessions.installParser(file.name, await file.text(), replace);
			await load();
		} catch (caught) {
			setError(caught instanceof Error ? caught.message : String(caught));
		} finally {
			setBusy(false);
		}
	}

	async function download(filename: string) {
		try {
			const blob = await api.downloadSessions.downloadParser(filename);
			const url = URL.createObjectURL(blob);
			const link = document.createElement('a');
			link.href = url;
			link.download = filename;
			link.click();
			setTimeout(() => URL.revokeObjectURL(url), 1000);
		} catch (caught) {
			setError(caught instanceof Error ? caught.message : String(caught));
		}
	}

	return <details className="download-options parser-info" onToggle={event => {
		if (event.target === event.currentTarget && event.currentTarget.open && !parsers && !loading && !error) void load();
	}}>
		<summary>Installed parsers</summary>
		<button type="button" onClick={() => void load()} disabled={loading}>Refresh</button>
		<div>
			<label>Parser bundle <input type="file" accept=".js,.mjs" disabled={busy}
										onChange={event => setFile(event.target.files?.[0])}/></label>
			<label><input type="checkbox" checked={replace} disabled={busy}
						  onChange={event => setReplace(event.target.checked)}/> Replace existing filename</label>
			<button type="button" disabled={!file || busy}
			        onClick={() => void upload()}>{busy ? 'Installing…' : 'Install parser'}</button>
		</div>
		{loading && <p role="status">Loading parsers…</p>}
		{error && <p role="alert">Could not load parsers: {error}</p>}
		{parsers?.length === 0 && <p className="muted">No parsers loaded.</p>}
		{parsers?.map((parser, index) => <details key={`${parser.id}-${parser.version}-${index}`}>
			<summary>{parser.id} · version {parser.version} · SDK {parser.sdkVersion ?? 'not specified'}</summary>
			{parser.filename && !parser.filename.includes('/') &&
                <button type="button" onClick={() => void download(parser.filename)}>Download parser</button>}
			<p>Options: {parser.options.map(option => option.name).join(', ') || 'None'}</p>
			<p>Handlers: {parser.handlers.map(handler => handler.id).join(', ') || 'None'}</p>
			<p>Discovery: {parser.discoveries.map(discovery => discovery.urlClass).join(', ') || 'None'}</p>
			<p>URL classes: {parser.urlClasses.map(urlClass => urlClass.id).join(', ') || 'None'}</p>
			<details>
				<summary>Full declaration</summary>
				<pre>{JSON.stringify(parser, null, 2)}</pre>
			</details>
		</details>)}
	</details>;
}
