/**
 * Embeddings — lists registered embedding models (GET /embeddings/models) and drives a resumable
 * backfill per model (POST /embeddings/generate), polling GET /jobs/{job_id}/status for progress.
 * Generation only: no search over the vectors, per the design spec's scope cut.
 */

import {useCallback, useEffect, useRef, useState} from 'react';
import type {PanelProps} from '../../host/types';

interface EmbeddingModel {
    model_id: number;
    model_name: string;
    dimensions: number;
    /** Whether a loaded module is currently routing this model name. */
    available: boolean;
    /**
     * Roughly how many records this model has embedded, from pg_class.reltuples. Absent when the
     * table has never been analysed — reported as unknown rather than zero, because "0 embeddings"
     * would read as "nothing to lose" on the delete confirmation.
     */
    embedding_estimate?: number;
}

/** Shape of the JSON `setJobResponse` publishes for an embedding backfill, both mid-run and final. */
interface EmbeddingProgress {
    model_name: string;
    embedded: number;
    failed: number;
    skipped: number;
    /** Present only for the "no module provides this model" early-exit — the job still "completes". */
    error?: string;
}

interface JobStatusResponse {
    job_id: number;
    status?: string; // 'running' | 'completed' | 'failed'
    completed?: boolean;
    error?: string;
    response?: EmbeddingProgress;
}

interface ActiveJob {
    jobId: number;
    data: JobStatusResponse | null;
}

const POLL_MS = 1000;

function jobFailed(data: JobStatusResponse | null): boolean {
    return data?.status === 'failed' || Boolean(data?.response?.error);
}

function jobFailureMessage(data: JobStatusResponse | null): string {
    return data?.error ?? data?.response?.error ?? 'unknown error';
}

/** Progress line under a model row while a backfill is active or has just finished. */
function EmbedProgress({data}: { data: JobStatusResponse }) {
    const failed = jobFailed(data);
    const stateClass = data.completed ? (failed ? ' state-error' : ' state-done') : '';
    const label = !data.completed ? 'Running…' : failed ? `Failed: ${jobFailureMessage(data)}` : 'Completed';

    return (
        <div className={`embed-progress${stateClass}`}>
            <span>{label}</span>
            {data.response && !data.response.error && (
                <span className="muted embed-progress-counts">
          {data.response.embedded} embedded · {data.response.failed} failed · {data.response.skipped} skipped
        </span>
            )}
        </div>
    );
}

function EmbeddingsPanel({host}: PanelProps) {
    const [models, setModels] = useState<EmbeddingModel[] | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [loading, setLoading] = useState(false);
    const [jobs, setJobs] = useState<Record<string, ActiveJob>>({});
    /** Which model the delete button has been pressed for once. Deleting is irreversible. */
    const [confirming, setConfirming] = useState<number | null>(null);
    const timers = useRef<Record<string, number>>({});

    const refreshModels = useCallback(
        async (signal?: AbortSignal) => {
            setLoading(true);
            try {
                const res = await host.http.fetch('/embeddings/models');
                if (!res.ok) throw new Error(`/embeddings/models → ${res.status}`);
                const data = (await res.json()) as EmbeddingModel[];
                if (signal?.aborted) return;
                setModels(data);
                setError(null);
            } catch (err) {
                if (signal?.aborted) return;
                setError(err instanceof Error ? err.message : String(err));
            } finally {
                if (!signal?.aborted) setLoading(false);
            }
        },
        [host],
    );

    useEffect(() => {
        const controller = new AbortController();
        void refreshModels(controller.signal);
        return () => controller.abort();
    }, [refreshModels]);

    // Stop every poller on unmount; a running backfill itself is untouched, only this panel's polling.
    useEffect(
        () => () => {
            for (const id of Object.values(timers.current)) window.clearInterval(id);
            timers.current = {};
        },
        [],
    );

    const pollJob = useCallback(
        (modelName: string, jobId: number) => {
            const existing = timers.current[modelName];
            if (existing !== undefined) window.clearInterval(existing);

            const tick = async () => {
                try {
                    const res = await host.http.fetch(`/jobs/${jobId}/status`);
                    if (!res.ok) throw new Error(`/jobs/${jobId}/status → ${res.status}`);
                    const data = (await res.json()) as JobStatusResponse;
                    setJobs((prev) => ({...prev, [modelName]: {jobId, data}}));

                    if (data.completed) {
                        const timerId = timers.current[modelName];
                        if (timerId !== undefined) {
                            window.clearInterval(timerId);
                            delete timers.current[modelName];
                        }
                        if (jobFailed(data)) {
                            host.ui.toast(`Embedding backfill for "${modelName}" failed: ${jobFailureMessage(data)}`, {kind: 'error'});
                        } else {
                            const r = data.response;
                            host.ui.toast(
                                r
                                    ? `Embedding backfill for "${modelName}" finished: ${r.embedded} embedded, ${r.failed} failed, ${r.skipped} skipped.`
                                    : `Embedding backfill for "${modelName}" finished.`,
                                {kind: 'success'},
                            );
                        }
                    }
                } catch (err) {
                    host.ui.toast(`Lost track of the backfill for "${modelName}": ${err instanceof Error ? err.message : String(err)}`, {
                        kind: 'error',
                    });
                    const timerId = timers.current[modelName];
                    if (timerId !== undefined) {
                        window.clearInterval(timerId);
                        delete timers.current[modelName];
                    }
                }
            };

            void tick();
            timers.current[modelName] = window.setInterval(() => void tick(), POLL_MS);
        },
        [host],
    );

    const startBackfill = useCallback(
        async (model: EmbeddingModel) => {
            try {
                const res = await host.http.fetch('/embeddings/generate', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({model_name: model.model_name}),
                });
                const body = (await res.json().catch(() => null)) as { job_id?: number; error?: string } | null;
                if (!res.ok || typeof body?.job_id !== 'number') {
                    throw new Error(body?.error ?? `/embeddings/generate → ${res.status}`);
                }
                host.ui.toast(`Backfill started for "${model.model_name}".`, {kind: 'info'});
                setJobs((prev) => ({...prev, [model.model_name]: {jobId: body.job_id!, data: null}}));
                pollJob(model.model_name, body.job_id);
            } catch (err) {
                host.ui.toast(`Failed to start backfill: ${err instanceof Error ? err.message : String(err)}`, {kind: 'error'});
            }
        },
        [host, pollJob],
    );

    const deleteModel = useCallback(
        async (model: EmbeddingModel) => {
            setConfirming(null);
            try {
                const res = await host.http.fetch(`/embeddings/models/${model.model_id}`, {method: 'DELETE'});
                if (!res.ok) {
                    // The server's messages say which model and why — a running backfill reads very
                    // differently from a missing one, and both are worth showing verbatim.
                    throw new Error((await res.text()) || `/embeddings/models → ${res.status}`);
                }
                host.ui.toast(`Deleted "${model.model_name}" and its embeddings.`, {kind: 'info'});
                await refreshModels();
            } catch (err) {
                host.ui.toast(`Failed to delete: ${err instanceof Error ? err.message : String(err)}`, {kind: 'error'});
            }
        },
        [host, refreshModels],
    );

    return (
        <div className="panel-body embeddings">
            <div className="embed-toolbar">
                <button type="button" className="toolbar-button" onClick={() => void refreshModels()}
                        disabled={loading}>
                    {loading ? 'Refreshing…' : 'Refresh'}
                </button>
                <span className="muted grow">
          {models ? `${models.length} model${models.length === 1 ? '' : 's'}` : ''}
        </span>
            </div>

            {error && <p className="error">{error}</p>}

            {models === null ? (
                <p className="muted">Loading…</p>
            ) : models.length === 0 ? (
                <p className="muted">No embedding models registered. Load a module that provides one and restart the
                    server.</p>
            ) : (
                <ul className="embed-list">
                    {models.map((model) => {
                        const job = jobs[model.model_name];
                        const running = job !== undefined && job.data?.completed !== true;
                        return (
                            <li key={model.model_id} className="embed-card">
                                <div className="embed-head">
                  <span className={`embed-avail ${model.available ? 'ok' : 'error'}`}>
                    {model.available ? 'available' : 'unavailable'}
                  </span>
                                    <span className="embed-name grow" title={model.model_name}>
                    {model.model_name}
                  </span>
                                    <span className="muted embed-dims">{model.dimensions}d</span>
                                    <button
                                        type="button"
                                        className="toolbar-button"
                                        disabled={!model.available || running}
                                        title={!model.available ? 'No loaded module currently provides this model' : undefined}
                                        onClick={() => void startBackfill(model)}
                                    >
                                        {running ? 'Running…' : 'Generate'}
                                    </button>
                                    <button
                                        type="button"
                                        className="toolbar-button"
                                        disabled={running}
                                        title={running ? 'A backfill is running for this model' : 'Delete this model and every embedding it holds'}
                                        onClick={() => setConfirming(model.model_id)}
                                    >
                                        Delete
                                    </button>
                                </div>
                                {confirming === model.model_id && (
                                    <div className="embed-confirm">
                                        {/* Stated rather than implied: a backfill over a large
                                            collection is hours of work, and only another backfill
                                            can replace it. */}
                                        <span>
                                            Delete <strong>{model.model_name}</strong> and{' '}
                                            {typeof model.embedding_estimate === 'number'
                                                ? `about ${model.embedding_estimate.toLocaleString()} embeddings`
                                                : 'every embedding it holds'}
                                            ? This cannot be undone; regenerating them means another full backfill.
                                        </span>
                                        <button type="button" className="toolbar-button"
                                                onClick={() => setConfirming(null)}>
                                            Cancel
                                        </button>
                                        <button type="button" className="toolbar-button state-error"
                                                onClick={() => void deleteModel(model)}>
                                            Delete permanently
                                        </button>
                                    </div>
                                )}
                                {job?.data && <EmbedProgress data={job.data}/>}
                            </li>
                        );
                    })}
                </ul>
            )}
        </div>
    );
}

export const embeddingsPanel = {
    type: 'embeddings',
    title: 'Embeddings',
    description: 'View registered embedding models and run backfills to fill in vectors.',
    component: EmbeddingsPanel,
    configVersion: 1,
    singleton: true,
} as const;
