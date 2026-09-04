import {useCallback, useEffect, useMemo, useState} from 'react';
import {api} from '../../api/client';
import type {DownloaderSecrets} from '../../api/types';
import type {PanelProps} from '../../host/types';

export function canonicalSecrets(secrets: DownloaderSecrets): string {
    return JSON.stringify(Object.entries(secrets).sort(([left], [right]) => left.localeCompare(right)));
}

export function validSecrets(value: unknown): value is DownloaderSecrets {
    if (typeof value !== 'object' || value === null || Array.isArray(value)) return false;
    return Object.entries(value).every(([key, secret]) => key.length > 0 && typeof secret === 'string');
}

function DownloaderSecretsPanel({host}: PanelProps) {
    const [secrets, setSecrets] = useState<DownloaderSecrets>({});
    const [saved, setSaved] = useState<DownloaderSecrets>({});
    const [visible, setVisible] = useState<Set<string>>(() => new Set());
    const [newKey, setNewKey] = useState('');
    const [newValue, setNewValue] = useState('');
    const [newVisible, setNewVisible] = useState(false);
    const [loading, setLoading] = useState(true);
    const [saving, setSaving] = useState(false);
    const [error, setError] = useState<string | null>(null);

    const refresh = useCallback(async (signal?: AbortSignal) => {
        setLoading(true);
        try {
            const fetched = await api.downloader.secrets(signal);
            if (!validSecrets(fetched)) throw new Error('The server returned an invalid secret map.');
            setSecrets(fetched);
            setSaved(fetched);
            setVisible(new Set());
            setError(null);
        } catch (caught) {
            if (caught instanceof DOMException && caught.name === 'AbortError') return;
            setError(caught instanceof Error ? caught.message : String(caught));
        } finally {
            if (!signal?.aborted) setLoading(false);
        }
    }, []);

    useEffect(() => {
        const controller = new AbortController();
        void refresh(controller.signal);
        return () => controller.abort();
    }, [refresh]);

    const entries = useMemo(
        () => Object.entries(secrets).sort(([left], [right]) => left.localeCompare(right)),
        [secrets],
    );
    const dirty = canonicalSecrets(secrets) !== canonicalSecrets(saved);
    const trimmedKey = newKey.trim();
    const duplicateKey = trimmedKey.length > 0 && Object.hasOwn(secrets, trimmedKey);

    function updateValue(key: string, value: string) {
        setSecrets((current) => ({...current, [key]: value}));
    }

    function toggleVisible(key: string) {
        setVisible((current) => {
            const next = new Set(current);
            if (next.has(key)) next.delete(key);
            else next.add(key);
            return next;
        });
    }

    function addSecret() {
        if (trimmedKey.length === 0 || duplicateKey) return;
        setSecrets((current) => ({...current, [trimmedKey]: newValue}));
        setNewKey('');
        setNewValue('');
        setNewVisible(false);
    }

    function reload() {
        if (dirty && !window.confirm('Discard unsaved downloader secret changes?')) return;
        void refresh();
    }

    async function save() {
        setSaving(true);
        try {
            const updated = await api.downloader.setSecrets(secrets);
            if (!validSecrets(updated)) throw new Error('The server returned an invalid secret map.');
            setSecrets(updated);
            setSaved(updated);
            setError(null);
            host.ui.toast('Downloader secrets saved.', {kind: 'success'});
        } catch (caught) {
            const message = caught instanceof Error ? caught.message : String(caught);
            setError(message);
            host.ui.toast(`Saving downloader secrets failed: ${message}`, {kind: 'error'});
        } finally {
            setSaving(false);
        }
    }

    return (
        <div className="panel-body downloader-secrets">
            <div className="secret-toolbar">
                <p className="muted grow">
                    Values are available immediately to downloader scripts and are stored in the database.
                </p>
                <button type="button" className="toolbar-button" disabled={loading || saving}
                        onClick={reload}>
                    Refresh
                </button>
                <button type="button" className="toolbar-button" disabled={!dirty || loading || saving}
                        onClick={() => void save()}>
                    {saving ? 'Saving…' : 'Save changes'}
                </button>
            </div>

            {error !== null && <p className="error">{error}</p>}
            {loading && entries.length === 0 && <p className="muted">Loading downloader secrets…</p>}
            {!loading && entries.length === 0 && <p className="muted">No downloader secrets are configured.</p>}

            {entries.length > 0 && (
                <div className="secret-list">
                    {entries.map(([key, value]) => {
                        const revealed = visible.has(key);
                        return (
                            <div key={key} className="secret-row">
                                <span className="secret-key mono" title={key}>{key}</span>
                                <input
                                    className="search-input secret-value"
                                    type={revealed ? 'text' : 'password'}
                                    value={value}
                                    disabled={saving}
                                    aria-label={`Value for ${key}`}
                                    autoComplete="new-password"
                                    spellCheck={false}
                                    onChange={(event) => updateValue(key, event.target.value)}
                                />
                                <button type="button" className="toolbar-button secret-reveal" disabled={saving}
                                        aria-label={`${revealed ? 'Hide' : 'Show'} ${key}`}
                                        onClick={() => toggleVisible(key)}>
                                    {revealed ? 'Hide' : 'Show'}
                                </button>
                            </div>
                        );
                    })}
                </div>
            )}

            <section className="secret-add">
                <h3>Add a secret</h3>
                <div className="secret-add-row">
                    <input
                        className="search-input secret-new-key"
                        value={newKey}
                        placeholder="Secret key, e.g. gelbooru.apiKey"
                        disabled={saving}
                        aria-label="New secret key"
                        autoComplete="off"
                        spellCheck={false}
                        onChange={(event) => setNewKey(event.target.value)}
                    />
                    <input
                        className="search-input secret-value"
                        type={newVisible ? 'text' : 'password'}
                        value={newValue}
                        placeholder="Secret value"
                        disabled={saving}
                        aria-label="New secret value"
                        autoComplete="new-password"
                        spellCheck={false}
                        onChange={(event) => setNewValue(event.target.value)}
                        onKeyDown={(event) => {
                            if (event.key === 'Enter') addSecret();
                        }}
                    />
                    <button type="button" className="toolbar-button secret-reveal" disabled={saving}
                            onClick={() => setNewVisible((current) => !current)}>
                        {newVisible ? 'Hide' : 'Show'}
                    </button>
                    <button type="button" className="toolbar-button"
                            disabled={saving || trimmedKey.length === 0 || duplicateKey} onClick={addSecret}>
                        Add
                    </button>
                </div>
                {duplicateKey && <p className="error">That key already exists. Edit its value above.</p>}
                <p className="muted secret-hint">Adding a row is local until you save changes.</p>
            </section>
        </div>
    );
}

export const downloaderSecretsPanel = {
    type: 'downloader-secrets',
    title: 'Downloader Secrets',
    description: 'Add and update credentials exposed to downloader scripts.',
    component: DownloaderSecretsPanel,
    configVersion: 1,
    singleton: true,
} as const;
