import {useEffect, useState, type FormEvent} from 'react';
import {api} from '../../api/client';
import type {JobStatus} from '../../api/types';
import type {PanelProps} from '../../host/types';

type JobKind = 'urls' | 'metadata' | 'cluster' | 'embeddings';

interface ClusterChoice {
    cluster_id: number;
    name: string
}

interface ModelChoice {
    model_name: string;
    available: boolean
}

const jobOptions = {
    urls: {label: 'Reorganize URLs', description: 'Sort URLs using URL class rules and merge matching URLs.'},
    metadata: {
        label: 'Rescan metadata',
        description: 'Parse stored files again to refresh their metadata. Leave record IDs empty to scan all records with files.'
    },
    cluster: {
        label: 'Scan cluster',
        description: 'Scan a storage cluster and fill in missing MIME types and metadata. Optionally verify file hashes.'
    },
    embeddings: {
        label: 'Generate missing embeddings',
        description: 'Resume embedding generation for records missing vectors from the selected model.'
    },
} satisfies Record<JobKind, { label: string; description: string }>;

export function JobLauncher({host, jobs, onQueued}: {
    host: PanelProps['host'];
    jobs: readonly { job_name?: string; status?: string }[];
    onQueued: (job: Pick<JobStatus, 'job_id' | 'job_name' | 'status' | 'completed'>) => void;
}) {
    const [kind, setKind] = useState<JobKind>('urls');
    const [recordIds, setRecordIds] = useState('');
    const [clusters, setClusters] = useState<ClusterChoice[]>([]);
    const [models, setModels] = useState<ModelChoice[]>([]);
    const [target, setTarget] = useState('');
    const [verifyHash, setVerifyHash] = useState(false);
    const [loading, setLoading] = useState(false);
    const [queueing, setQueueing] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [message, setMessage] = useState<string | null>(null);
    const [reload, setReload] = useState(0);

    useEffect(() => {
        if (kind !== 'cluster' && kind !== 'embeddings') return;
        const controller = new AbortController();

        async function loadChoices() {
            setLoading(true);
            setError(null);
            try {
                const path = kind === 'cluster' ? '/clusters/list' : '/embeddings/models';
                const response = await host.http.fetch(path, {signal: controller.signal});
                if (!response.ok) throw new Error(await response.text() || `Could not load choices (${response.status})`);
                if (kind === 'cluster') {
                    const choices = await response.json() as ClusterChoice[];
                    if (controller.signal.aborted) return;
                    setClusters(choices);
                    setTarget(String(choices[0]?.cluster_id ?? ''));
                } else {
                    const choices = await response.json() as ModelChoice[];
                    if (controller.signal.aborted) return;
                    setModels(choices);
                    setTarget(choices.find(model => model.available)?.model_name ?? '');
                }
            } catch (error) {
                if (!controller.signal.aborted) setError(error instanceof Error ? error.message : String(error));
            } finally {
                if (!controller.signal.aborted) setLoading(false);
            }
        }

        void loadChoices();
        return () => controller.abort();
    }, [kind, host, reload]);

    const jobName = kind === 'urls' ? 'Reorganizing URLs'
        : kind === 'metadata' ? 'Rescanning record metadata'
            : kind === 'cluster' ? `Scanning cluster ${target}` : `Embedding backfill for ${target}`;
    const running = jobs.some(job => job.job_name === jobName && job.status === 'running');
    const needsTarget = kind === 'cluster' || kind === 'embeddings';

    async function start(event: FormEvent) {
        event.preventDefault();
        if (queueing || running || loading || (needsTarget && !target)) return;
        setError(null);
        setMessage(null);
        setQueueing(true);
        try {
            let job: { job_id: number };
            if (kind === 'urls') {
                job = await api.reorganizeUrls();
            } else if (kind === 'metadata') {
                // Accept positive decimal record IDs separated by whitespace or commas.
                const tokens = recordIds.trim() ? recordIds.trim().split(/[\s,]+/) : [];
                if (tokens.some(token => !/^\d+$/.test(token) || Number(token) <= 0 || Number(token) > 2147483647)) {
                    throw new Error('Enter positive record IDs up to 2147483647, separated by spaces or commas.');
                }
                job = await api.rescanMetadata([...new Set(tokens.map(Number))]);
            } else {
                const response = await host.http.fetch(kind === 'cluster'
                    ? `/clusters/${target}/scan?verify_hash=${verifyHash}` : '/embeddings/generate', {
                    method: 'POST',
                    ...(kind === 'embeddings' ? {
                        headers: {'Content-Type': 'application/json'}, body: JSON.stringify({model_name: target}),
                    } : {}),
                });
                if (!response.ok) throw new Error(await response.text() || `Could not start job (${response.status})`);
                job = await response.json() as { job_id: number };
            }
            onQueued({job_id: job.job_id, job_name: jobName, status: 'running', completed: false});
            setMessage(`Queued ${jobOptions[kind].label.toLowerCase()} as job #${job.job_id}.`);
        } catch (error) {
            setError(error instanceof Error ? error.message : String(error));
        } finally {
            setQueueing(false);
        }
    }

    return <form className="job-launcher" onSubmit={event => void start(event)}>
        <div className="job-launcher-fields">
            <label>Job to start
                <select value={kind} disabled={queueing} onChange={event => {
                    setKind(event.target.value as JobKind);
                    setTarget('');
                    setClusters([]);
                    setModels([]);
                    setLoading(false);
                    setError(null);
                    setMessage(null);
                }}>
                    {Object.entries(jobOptions).map(([value, option]) => <option key={value}
                                                                                 value={value}>{option.label}</option>)}
                </select>
            </label>
            {kind === 'metadata' && <label>Record IDs (optional)
                <input value={recordIds} onChange={event => setRecordIds(event.target.value)}
                       placeholder="All records with files" disabled={queueing}/>
            </label>}
            {kind === 'cluster' && <label>Cluster
                <select value={target} onChange={event => setTarget(event.target.value)} disabled={loading || queueing}>
                    {!clusters.length &&
                        <option value="">{loading ? 'Loading clusters…' : 'No clusters available'}</option>}
                    {clusters.map(cluster => <option key={cluster.cluster_id}
                                                     value={cluster.cluster_id}>{cluster.name}</option>)}
                </select>
            </label>}
            {kind === 'embeddings' && <label>Model
                <select value={target} onChange={event => setTarget(event.target.value)} disabled={loading || queueing}>
                    {!target && <option value="">{loading ? 'Loading models…' : 'No available model selected'}</option>}
                    {models.map(model => <option key={model.model_name} value={model.model_name}
                                                 disabled={!model.available}>
                        {model.model_name}{model.available ? '' : ' (unavailable)'}
                    </option>)}
                </select>
            </label>}
            <button type="submit" disabled={queueing || running || loading || (needsTarget && !target)}>
                {queueing ? 'Queueing…' : running ? 'Already running' : jobOptions[kind].label}
            </button>
            {needsTarget && <button type="button" disabled={loading || queueing}
                                    onClick={() => setReload(value => value + 1)}>Refresh choices</button>}
        </div>
        <p className="muted">{jobOptions[kind].description}</p>
        {kind === 'cluster' && <label className="job-auto">
            <input type="checkbox" checked={verifyHash} onChange={event => setVerifyHash(event.target.checked)}
                   disabled={queueing}/> Verify file hashes
        </label>}
        {error && <p className="error" role="alert">{error}</p>}
        {message && <p role="status">{message}</p>}
    </form>;
}
