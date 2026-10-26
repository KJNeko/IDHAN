export interface UrlReorganizationProgressData {
    kind: 'url_reorganization';
    phase: string;
    message: string;
    processed_count: number;
    total_count?: number;
    sorted_count: number;
    unique_count?: number;
    merge_candidate_count?: number;
    merged_count: number;
    mappings_processed_count?: number;
    committed_batch_count?: number;
}

export function UrlReorganizationProgress({progress}: { progress: UrlReorganizationProgressData }) {
    return <div className="job-progress" aria-label="URL reorganization progress">
        <span role="status">{progress.message}</span>
        <span className="muted">
            Checked: {progress.processed_count.toLocaleString()}
            {progress.total_count !== undefined && <> / {progress.total_count.toLocaleString()}</>}
            {' · '}Sorted: {progress.sorted_count.toLocaleString()}
        </span>
        {progress.total_count !== undefined && <>
            <progress aria-label="URLs checked" value={progress.processed_count}
                      max={Math.max(1, progress.total_count)}/>
            <span className="muted">
                Total: {progress.total_count.toLocaleString()}
                {progress.unique_count !== undefined && <> · Unique: {progress.unique_count.toLocaleString()}</>}
                {' · '}Merged: {progress.merged_count.toLocaleString()}
                {progress.merge_candidate_count !== undefined && progress.phase !== 'completed'
                    && <> · Planned merges: {progress.merge_candidate_count.toLocaleString()}</>}
            </span>
        </>}
        {progress.committed_batch_count !== undefined && <span className="muted">
            Committed batches: {progress.committed_batch_count.toLocaleString()}
            {' · '}Mappings processed: {(progress.mappings_processed_count ?? 0).toLocaleString()}
        </span>}
    </div>;
}
