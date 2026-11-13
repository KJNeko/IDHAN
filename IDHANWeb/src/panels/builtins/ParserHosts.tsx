import {useState} from 'react';
import {api} from '../../api/client';
import type {DownloaderParserHost} from '../../api/types';

export function ParserHosts({parser, disabled}: { parser: string; disabled: boolean }) {
    const [hosts, setHosts] = useState<DownloaderParserHost[]>();
    const [draft, setDraft] = useState('');
    const [busy, setBusy] = useState(false);
    const [error, setError] = useState<string>();

    async function refresh() {
        setHosts((await api.downloaderHosts.list()).filter(entry => entry.parser === parser));
    }

    async function update(action?: () => Promise<unknown>) {
        setBusy(true);
        setError(undefined);
        try {
            if (action) await action();
            await refresh();
        } catch (caught) {
            setError(caught instanceof Error ? caught.message : String(caught));
        } finally {
            setBusy(false);
        }
    }

    function add() {
        const host = draft.trim();
        if (!host || busy || disabled) return;
        void update(async () => {
            await api.downloaderHosts.set(host, parser);
            setDraft('');
        });
    }

    return <details className="download-parser-hosts" onToggle={event => {
        if (event.target === event.currentTarget && event.currentTarget.open && !busy) void update();
    }}>
        <summary>Hosts</summary>
		<div className="download-hosts-body">

        {busy && <p role="status">Updating hosts…</p>}
        {error && <p role="alert">{error}
            <button type="button" className="toolbar-button" disabled={busy}
                    onClick={() => void update()}>Retry
            </button>
        </p>}
        {hosts?.length === 0 && <p className="muted">No saved hosts.</p>}
        <ul>
            {hosts?.map(entry => <li key={entry.host}>
                <span>{entry.host} — {entry.auto_discovered ? 'Auto-discovered' : 'User added'}</span>{' '}
				<button type="button" className="toolbar-button" disabled={disabled || busy}
	                    aria-label={`Remove ${entry.host}`}
                        onClick={() => void update(() => api.downloaderHosts.remove(entry.host))}>Remove
                </button>
            </li>)}
        </ul>
			<div className="download-host-add">
				<label>
            <span>Hostname</span>
            <input type="text" value={draft} disabled={disabled || busy} placeholder="hyshare.example.com"
                   onChange={event => setDraft(event.target.value)} onKeyDown={event => {
                if (event.key === 'Enter') {
                    event.preventDefault();
                    add();
                }
            }}/>
        </label>
				<button type="button" className="toolbar-button" disabled={disabled || busy || !draft.trim()}
			            onClick={add}>Add host
			    </button>
			</div>
		</div>
    </details>;
}
