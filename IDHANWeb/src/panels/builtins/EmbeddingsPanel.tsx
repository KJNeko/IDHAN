/**
 * Embeddings: lists registered embedding models (GET /embeddings/models) and drives a resumable
 * backfill per model (POST /embeddings/generate), receiving job progress through SSE.
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
     * table has never been analysed, so the UI reports unknown rather than a misleading zero.
     */
    embedding_estimate?: number;
}

/** Shape of the JSON `setJobResponse` publishes for an embedding backfill, both mid-run and final. */
interface EmbeddingProgress {
    model_name: string;
    embedded: number;
    failed: number;
    skipped: number;
    /** Present only when no module provides the model. The job still completes. */
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
    const subscriptions = useRef<Record<string, () => void>>({});

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

    // Stop subscriptions on unmount; running backfills continue on the server.
    useEffect(
        () => () => {
            for (const close of Object.values(subscriptions.current)) close();
            subscriptions.current = {};
        },
        [],
    );

    const watchBackfill = useCallback(
        (modelName: string, jobId: number) => {
            subscriptions.current[modelName]?.();
            const subscription = host.jobs.watch(jobId, (status) => {
                const data = status as JobStatusResponse;
                setJobs((prev) => ({...prev, [modelName]: {jobId, data}}));
                if (!data.completed) return;
                delete subscriptions.current[modelName];
                if (jobFailed(data)) {
                    host.ui.toast(`Embedding backfill for "${modelName}" failed: ${jobFailureMessage(data)}`, {kind: 'error'});
                } else {
                    const r = data.response;
                    host.ui.toast(r
                        ? `Embedding backfill for "${modelName}" finished: ${r.embedded} embedded, ${r.failed} failed, ${r.skipped} skipped.`
                        : `Embedding backfill for "${modelName}" finished.`, {kind: 'success'});
                }
            });
            subscriptions.current[modelName] = subscription.cancel;
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
                watchBackfill(model.model_name, body.job_id);
            } catch (err) {
                host.ui.toast(`Failed to start backfill: ${err instanceof Error ? err.message : String(err)}`, {kind: 'error'});
            }
        },
        [host, watchBackfill],
    );

    const deleteModel = useCallback(
        async (model: EmbeddingModel) => {
            setConfirming(null);
            try {
                const res = await host.http.fetch(`/embeddings/models/${model.model_id}`, {method: 'DELETE'});
                if (!res.ok) {
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
