import {PanelShortcut} from '../PanelShortcut';
import {useEffect, useMemo, useRef, useState, type DragEvent} from 'react';
import type {TagDomain} from '../../api/types';
import type {PanelProps} from '../../host/types';
import {
    captureDroppedFiles,
    collectImportFiles,
    describeImportError,
    type ImportFile,
    type ImportSelection
} from './importFiles';
import {runImportPool, uploadImportFile, type FileImportResult} from './fileImport';
import {folderNames, parseImportTags, planFolderTags, type FolderTagPlan} from './folderImportPlan';
import {ImportStorageNotice, useImportStorage} from './importStorage';
import {formatBytes} from './RecordInfoView';

type State = 'ready' | 'uploading' | 'tagging' | 'done' | 'skipped' | 'error';

interface Entry {
    source: ImportFile;
    state: State;
    result?: FileImportResult;
    error?: string;
}

interface PreviewNode {
    name: string;
    folders: Map<string, PreviewNode>;
    files: number[];
}

function buildPreview(entries: Entry[]): PreviewNode {
    const root: PreviewNode = {name: '', folders: new Map(), files: []};
    entries.forEach((entry, index) => {
        let node = root;
        for (const folder of entry.source.path.split('/').slice(0, -1)) {
            let next = node.folders.get(folder);
            if (!next) {
                next = {name: folder, folders: new Map(), files: []};
                node.folders.set(folder, next);
            }
            node = next;
        }
        node.files.push(index);
    });
    return root;
}

function PreviewTree({node, entries, plans}: { node: PreviewNode; entries: Entry[]; plans: FolderTagPlan[] }) {
    return <ul className="advanced-import-tree">
        {[...node.folders.values()].map(folder => <li key={folder.name}>
            <div className="advanced-import-folder">{folder.name}/</div>
            <PreviewTree node={folder} entries={entries} plans={plans}/>
        </li>)}
        {node.files.map(index => {
            const entry = entries[index]!;
            const plan = plans[index]!;
            const status = entry.state === 'ready' ? formatBytes(entry.source.file.size)
                : entry.state === 'done' ? `${entry.result?.outcome === 'exists' ? 'Already present' : 'Imported'} #${entry.result?.recordId}`
                    : entry.state === 'skipped' ? `Previously deleted #${entry.result?.recordId}` : entry.state;
            return <li key={index} className={`advanced-import-file state-${entry.state}`}>
                <div className="advanced-import-file-head"><span>{entry.source.file.name}</span><span
                    className="muted">{status}</span></div>
                {plan.tags.length > 0 && <div className="advanced-import-tags">{plan.tags.join(' · ')}</div>}
                {plan.errors.map(error => <p key={error} className="error" role="alert">{error}</p>)}
                {entry.error && <p className="error" role="alert">{entry.error}</p>}
            </li>;
        })}
    </ul>;
}

function AdvancedImportPanel({host}: PanelProps) {
    const storage = useImportStorage(host);
    const [entries, setEntries] = useState<Entry[]>([]);
    const [readErrors, setReadErrors] = useState<string[]>([]);
    const [domains, setDomains] = useState<TagDomain[]>([]);
    const [domainId, setDomainId] = useState<number | null>(null);
    const [domainsLoading, setDomainsLoading] = useState(true);
    const [domainError, setDomainError] = useState<string | null>(null);
    const [domainReload, setDomainReload] = useState(0);
    const [namespaces, setNamespaces] = useState(['artist', 'character']);
    const [commonTags, setCommonTags] = useState('');
    const [includeRoot, setIncludeRoot] = useState(true);
    const [forceImport, setForceImport] = useState(false);
    const [busy, setBusy] = useState(false);
    const [reading, setReading] = useState(false);
    const [empty, setEmpty] = useState(false);
    const folderInput = useRef<HTMLInputElement>(null);
    const pending = useRef(false);
    const lifetime = useRef<AbortController | null>(null);

    useEffect(() => {
        const controller = new AbortController();
        lifetime.current = controller;
        return () => controller.abort();
    }, []);

    useEffect(() => {
        const controller = new AbortController();
        setDomainError(null);
        setDomainsLoading(true);
        host.tags.listDomains(controller.signal).then(list => {
            if (controller.signal.aborted) return;
            setDomains(list);
            setDomainId(current => list.some(domain => domain.tag_domain_id === current) ? current : list[0]?.tag_domain_id ?? null);
        }).catch((error: unknown) => {
            if (!controller.signal.aborted) setDomainError(`Could not load tag domains: ${describeImportError(error)}`);
        }).finally(() => {
            if (!controller.signal.aborted) setDomainsLoading(false);
        });
        return () => controller.abort();
    }, [host, domainReload]);

    const depth = entries.reduce((maximum, entry) => Math.max(maximum, folderNames(entry.source, includeRoot).length), 0);
    const mappedNamespaces = Array.from({length: depth}, (_, index) => namespaces[index] ?? '');
    const sharedTags = parseImportTags(commonTags);
    const plans = entries.map(entry => planFolderTags(entry.source, mappedNamespaces, includeRoot, sharedTags));
    const mappingErrors = plans.some(plan => plan.errors.length > 0);
    const needsDomain = sharedTags.length > 0 || plans.some(plan => plan.tags.length > 0);
    const tree = useMemo(() => buildPreview(entries), [entries]);
    const remaining = entries.filter(entry => entry.state === 'ready' || entry.state === 'error').length;
    const done = entries.filter(entry => entry.state === 'done').length;

    async function choose(selection: ImportSelection) {
        if (pending.current) return;
        pending.current = true;
        setReading(true);
        setReadErrors([]);
        setEmpty(false);
        try {
            const collected = await collectImportFiles(selection);
            if (lifetime.current?.signal.aborted) return;
            setEntries(collected.files.map(source => ({source, state: 'ready'})));
            setReadErrors(collected.errors);
            setEmpty(collected.files.length === 0 && collected.errors.length === 0);
        } catch (error) {
            setReadErrors([`Could not read the selected folders: ${describeImportError(error)}`]);
        } finally {
            pending.current = false;
            setReading(false);
        }
    }

    function drop(event: DragEvent) {
        event.preventDefault();
        if (!pending.current) void choose(captureDroppedFiles(event.dataTransfer));
    }

    function patch(index: number, changes: Partial<Entry>) {
        setEntries(current => current.map((entry, position) => position === index ? {...entry, ...changes} : entry));
    }

    async function start() {
        if (pending.current || mappingErrors || remaining === 0 || (needsDomain && domainId === null)) return;
        pending.current = true;
        setBusy(true);
        const signal = lifetime.current?.signal;
        try {
            if (!await storage.check(signal)) return;
            const completed: number[] = [];
            const domainName = domains.find(domain => domain.tag_domain_id === domainId)?.domain_name;
            await runImportPool(entries.map((entry, index) => ({entry, index})), async ({entry, index}) => {
                if (signal?.aborted || (entry.state !== 'ready' && entry.state !== 'error')) return;
                let result = entry.result;
                try {
                    patch(index, {state: result ? 'tagging' : 'uploading', error: undefined});
                    if (!result) result = await uploadImportFile(host, entry.source.file, forceImport, signal);
                    patch(index, {result});
                    if (result.outcome === 'deleted') {
                        patch(index, {state: 'skipped'});
                        return;
                    }
                    const tags = plans[index]!.tags;
                    if (tags.length > 0 && domainId !== null) {
                        patch(index, {state: 'tagging'});
                        try {
                            await host.tags.addToRecords([result.recordId], tags, domainId, signal);
                        } catch (error) {
                            throw new Error(`File "${entry.source.file.name}" is stored as #${result.recordId}, but applying tags to ${domainName ?? domainId} failed: ${describeImportError(error)}. Retry will apply tags without uploading again.`);
                        }
                    }
                    patch(index, {state: 'done'});
                    completed.push(result.recordId);
                } catch (error) {
                    if (!signal?.aborted) patch(index, {state: 'error', result, error: describeImportError(error)});
                }
            });
            if (!signal?.aborted && completed.length > 0) host.selection.set([...new Set(completed)]);
        } catch (error) {
            setReadErrors(previous => [...previous, `Could not start importing: ${describeImportError(error)}`]);
        } finally {
            pending.current = false;
            setBusy(false);
        }
    }

    // Freeze tags and their domain once uploads begin so retries keep the reviewed plan.
    const started = entries.some(entry => entry.state !== 'ready');
    const locked = busy || reading || started;
    return <div className="panel-body advanced-import">
        <ImportStorageNotice storage={storage}/>
        <div className="import-drop" onDragOver={event => event.preventDefault()} onDrop={drop}>
            <button type="button" className="toolbar-button" disabled={busy || reading}
                    onClick={() => folderInput.current?.click()}>Choose folder…
            </button>
            <input type="file" multiple hidden aria-label="Preview folder"
                   ref={node => {
                       folderInput.current = node;
                       if (node) node.webkitdirectory = true;
                   }} disabled={busy || reading}
                   onChange={event => {
                       void choose({sources: Array.from(event.target.files ?? [])});
                       event.target.value = '';
                   }}/>
        </div>
        <div className="panel-row">
            <label className="tag-domain">Tag domain
                <select aria-label="Tag domain" disabled={locked || domains.length === 0} value={domainId ?? ''}
                        onChange={event => setDomainId(Number(event.target.value))}>
                    {domains.length === 0 &&
                        <option value="">{domainsLoading ? 'Loading domains…' : 'No tag domains'}</option>}
                    {domains.map(domain => <option key={domain.tag_domain_id}
                                                   value={domain.tag_domain_id}>{domain.domain_name}</option>)}
                </select>
            </label>
            <label className="panel-checkbox"><input type="checkbox" checked={includeRoot} disabled={locked}
                                                     onChange={event => setIncludeRoot(event.target.checked)}/>Include
                selected folder</label>
        </div>
        {domainError && <p className="error" role="alert">{domainError}
            <button type="button" className="toolbar-button"
                    onClick={() => setDomainReload(current => current + 1)}>Retry
            </button>
        </p>}
        {needsDomain && !domainsLoading && !domainError && domains.length === 0 &&
            <div className="panel-row" role="alert"><span className="error">No tag domain.</span><PanelShortcut
                type="tag-domain-manager">Add tag domain</PanelShortcut></div>}
        <label className="advanced-import-common">Tags for all files
            <textarea className="search-input" aria-label="Tags for all files" rows={2}
                      placeholder="One tag per line" value={commonTags} disabled={locked}
                      onChange={event => setCommonTags(event.target.value)}/>
        </label>
        {depth > 0 && <div className="advanced-import-mapping">
			{mappedNamespaces.map((namespace, index) => <label key={index}>Folder level {index + 1} namespace
                <input className="search-input" aria-label={`Namespace for folder level ${index + 1}`} value={namespace}
                       placeholder="Skip" disabled={locked} onChange={event => setNamespaces(current => {
                    const next = [...current];
                    next[index] = event.target.value;
                    return next;
                })}/>
            </label>)}
        </div>}
        <div className="panel-row">
            <label className="panel-checkbox"><input type="checkbox" checked={forceImport} disabled={busy || reading}
                                                     onChange={event => setForceImport(event.target.checked)}/>Force
                unknown file types</label>
            <button type="button" className="toolbar-button"
                    disabled={busy || reading || !storage.canImport || mappingErrors || remaining === 0 || (needsDomain && domainId === null)}
                    onClick={() => void start()}>{busy ? 'Importing…' : started ? 'Retry failed' : 'Import files'}</button>
            {entries.length > 0 && <span className="muted"
                                         role="status">{entries.length.toLocaleString()} files · {done.toLocaleString()} complete</span>}
            {reading && <span className="muted" role="status">Reading folders…</span>}
        </div>
        {readErrors.length > 0 && <ul className="import-read-errors" role="alert">{readErrors.map((error, index) => <li
            key={index}>{error}</li>)}</ul>}
        {empty && <p className="muted" role="status">No files found in the selected folders.</p>}
        <PreviewTree node={tree} entries={entries} plans={plans}/>
    </div>;
}

export const advancedImportPanel = {
    type: 'advanced-import', title: 'Advanced Import',
	description: 'Folder imports and tag mapping.',
    component: AdvancedImportPanel, configVersion: 1,
} as const;
