import {useCallback, useEffect, useMemo, useState} from 'react';
import {api} from '../../api/client';
import type {DownloaderSecrets, DownloaderSecretValue} from '../../api/types';
import type {PanelProps} from '../../host/types';

function orderedSecret(value: DownloaderSecretValue): DownloaderSecretValue {
    if (Array.isArray(value)) return value.map(orderedSecret);
    if (value === null || typeof value !== 'object') return value;
    return Object.fromEntries(Object.entries(value).sort(([a], [b]) => a.localeCompare(b))
        .map(([key, entry]) => [key, orderedSecret(entry)]));
}

export function canonicalSecrets(secrets: DownloaderSecrets): string {
    return JSON.stringify(orderedSecret(secrets));
}

export function normalizeSecretDomain(domain: string): string {
    if (domain.length === 0 || [...'/\\?#@'].some((character) => domain.includes(character))) {
        throw new Error('Enter a bare root domain, such as e621.net.');
    }
    if (domain.includes(':') && !domain.endsWith(']')) throw new Error('Domains must not include a port.');
    const hostname = new URL(`https://${domain}/`).hostname;
    return hostname.endsWith('.') ? hostname.slice(0, -1) : hostname;
}

function isSecretValue(value: unknown): value is DownloaderSecretValue {
    if (value === null || typeof value === 'string' || typeof value === 'boolean') return true;
    if (typeof value === 'number') return Number.isFinite(value);
    if (Array.isArray(value)) return value.every(isSecretValue);
    return typeof value === 'object' && Object.values(value).every(isSecretValue);
}

export function validSecrets(value: unknown): value is DownloaderSecrets {
    if (typeof value !== 'object' || value === null || Array.isArray(value)) return false;
    return Object.entries(value).every(([domain, document]) => {
        try {
            return normalizeSecretDomain(domain) === domain
                && typeof document === 'object' && document !== null && !Array.isArray(document)
                && isSecretValue(document);
        } catch {
            return false;
        }
    });
}

export function parseSecretObject(text: string): DownloaderSecrets[string] {
    let value: unknown;
    try {
        value = JSON.parse(text);
    } catch {
        throw new Error('Secrets must contain valid JSON.');
    }
    if (typeof value !== 'object' || value === null || Array.isArray(value) || !isSecretValue(value)) {
        throw new Error('Secrets must be a JSON object.');
    }
    return value as DownloaderSecrets[string];
}

function SecretFields({text, disabled, onChange}: {
    text: string;
    disabled: boolean;
    onChange: (text: string) => void
}) {
    const [name, setName] = useState('');
    const [shown, setShown] = useState<Set<string>>(() => new Set());
    const object = parseSecretObject(text);

    function update(key: string, value: DownloaderSecretValue) {
        onChange(JSON.stringify({...object, [key]: value}));
    }

    function remove(key: string) {
        const next = {...object};
        delete next[key];
        onChange(JSON.stringify(next));
    }

    return <div className="secret-list">
        {Object.entries(object).map(([key, value]) => <div className="secret-row" key={key}>
            <span className="secret-key mono">{key}</span>
            {typeof value === 'string' ? <input className="search-input secret-value"
                                                type={shown.has(key) ? 'text' : 'password'} value={value}
                                                disabled={disabled}
                                                aria-label={`Value for ${key}`} autoComplete="new-password"
                                                onChange={(event) => update(key, event.target.value)}/> :
                <span className="secret-value muted">{shown.has(key) ? JSON.stringify(value) : '••••••••'} (existing non-text value)</span>}
            <button type="button" className="toolbar-button secret-icon"
                    aria-label={`${shown.has(key) ? "Hide" : "Show"} ${key}`}
                    title={shown.has(key) ? "Hide value" : "Show value"} onClick={() => setShown((current) => {
                const next = new Set(current);
                if (next.has(key)) next.delete(key); else next.add(key);
                return next;
            })}><span aria-hidden="true">{shown.has(key) ? '◉' : '◎'}</span></button>
            <button type="button" className="toolbar-button secret-icon" disabled={disabled} onClick={() => remove(key)}
                    aria-label={`Remove ${key}`} title="Remove field"><span aria-hidden="true">×</span></button>
        </div>)}
        <div className="secret-add-field">
			<input className="search-input secret-value" placeholder="Field name"
                   aria-label="New field name"
                   value={name} disabled={disabled} onChange={(event) => setName(event.target.value)}/>
            <button type="button" className="toolbar-button secret-icon" aria-label="Add field" title="Add field"
                    disabled={disabled || !name.trim() || Object.hasOwn(object, name.trim())}
                    onClick={() => {
                        update(name.trim(), '');
                        setName('');
                    }}><span aria-hidden="true">+</span></button>
        </div>
    </div>;
}

function DownloaderSecretsPanel({host}: PanelProps) {
    const [documents, setDocuments] = useState<Record<string, string>>({});
    const [saved, setSaved] = useState<DownloaderSecrets>({});
    const [newKey, setNewKey] = useState('');
    const [loading, setLoading] = useState(true);
    const [saving, setSaving] = useState(false);
    const [error, setError] = useState<string | null>(null);

    const refresh = useCallback(async (signal?: AbortSignal) => {
        setLoading(true);
        try {
            const fetched = await api.downloader.secrets(signal);
            if (!validSecrets(fetched)) throw new Error('The server returned an invalid secret map.');
            setDocuments(Object.fromEntries(Object.entries(fetched).map(([domain, value]) => [domain, JSON.stringify(value)])));
            setSaved(fetched);
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
        () => Object.entries(documents).sort(([left], [right]) => left.localeCompare(right)),
        [documents],
    );
    const parsed = useMemo(() => {
        try {
            const value: DownloaderSecrets = Object.fromEntries(entries.map(([domain, text]) => [domain, parseSecretObject(text)]));
            return {value, error: null};
        } catch (caught) {
            return {value: null, error: caught instanceof Error ? caught.message : String(caught)};
        }
    }, [entries]);
    const dirty = parsed.value === null || canonicalSecrets(parsed.value) !== canonicalSecrets(saved);
    const trimmedKey = newKey.trim();
    let newDomain = '';
    let addError: string | null = null;
    try {
        if (trimmedKey.length > 0) newDomain = normalizeSecretDomain(trimmedKey);
    } catch (caught) {
        addError = caught instanceof Error ? caught.message : String(caught);
    }
    const duplicateKey = Object.hasOwn(documents, newDomain);

    function updateValue(key: string, value: string) {
        setDocuments((current) => ({...current, [key]: value}));
    }

    function addSecret() {
        if (newDomain.length === 0 || duplicateKey || addError !== null) return;
        setDocuments((current) => ({...current, [newDomain]: '{}'}));
        setNewKey('');
    }

    function reload() {
        if (dirty && !window.confirm('Discard unsaved downloader secret changes?')) return;
        void refresh();
    }

    async function save() {
        if (parsed.value === null) return;
        setSaving(true);
        try {
            const updated = await api.downloader.setSecrets(parsed.value);
            if (!validSecrets(updated)) throw new Error('The server returned an invalid secret map.');
            setDocuments(Object.fromEntries(Object.entries(updated).map(([domain, value]) => [domain, JSON.stringify(value)])));
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
                <span className="muted grow">{entries.length} domains{dirty ? ' · Unsaved changes' : ''}</span>
                <button type="button" className="toolbar-button secret-icon" disabled={loading || saving}
                        aria-label="Refresh" title="Refresh"
                        onClick={reload}>
                    <span aria-hidden="true">↻</span>
                </button>
                <button type="button" className="toolbar-button secret-icon" aria-label="Save changes"
                        title="Save changes"
                        disabled={!dirty || loading || saving || parsed.value === null}
                        onClick={() => void save()}>
                    <span aria-hidden="true">{saving ? '…' : '✓'}</span>
                </button>
            </div>

            {error !== null && <p className="error">{error}</p>}
            {parsed.error !== null && <p className="error">{parsed.error}</p>}

            {loading && entries.length === 0 && <p className="muted">Loading downloader secrets…</p>}
            {!loading && entries.length === 0 && <p className="muted">No downloader secrets are configured.</p>}

            {entries.length > 0 && (
                <div className="secret-list">
                    {entries.map(([key, value]) => (
                        <div key={key} className="secret-domain">
                            <div className="secret-domain-header">
                                <span className="secret-key mono">{key}</span>
                                <button type="button" className="toolbar-button secret-icon"
                                        aria-label={`Remove domain ${key}`} title="Remove domain" disabled={saving}
                                        onClick={() => {
                                            setDocuments((current) => {
                                                const next = {...current};
                                                delete next[key];
                                                return next;
                                            });
                                        }}><span aria-hidden="true">×</span></button>
                            </div>
                            <SecretFields text={value} disabled={saving} onChange={(text) => updateValue(key, text)}/>
                        </div>
                    ))}
                </div>
            )}

            <section className="secret-add">
                <div className="secret-add-row">
                    <input
                        className="search-input secret-new-key"
                        value={newKey}
						placeholder="Root domain"
                        disabled={saving}
                        aria-label="Root domain"
                        autoComplete="off"
                        spellCheck={false}
                        onChange={(event) => setNewKey(event.target.value)}
                    />
                    <button type="button" className="toolbar-button secret-icon" aria-label="Add domain"
                            title="Add domain"
                            disabled={saving || newDomain.length === 0 || duplicateKey || addError !== null}
                            onClick={addSecret}>
                        <span aria-hidden="true">+</span>
                    </button>
                </div>
                {addError !== null && <p className="error">{addError}</p>}
                {duplicateKey && <p className="error">That domain already exists. Edit its fields above.</p>}
            </section>
        </div>
    );
}

export const downloaderSecretsPanel = {
    type: 'downloader-secrets',
    title: 'Downloader Secrets',
	description: 'Downloader credentials.',
    component: DownloaderSecretsPanel,
    configVersion: 1,
    singleton: true,
} as const;
