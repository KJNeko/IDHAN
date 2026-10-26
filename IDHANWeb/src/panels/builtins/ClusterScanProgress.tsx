export interface ClusterScanProgressData {
    kind: 'cluster_scan';
    phase: string;
    checked_records: number;
    counted_files: number;
    processed_files: number;
    processed_bytes: number;
    failed_count: number;
    active_files: number;
    total_files?: number;
    remaining_files?: number;
    last_failure?: { path: string; error: string };
    tasks: { task_id: number; path: string; stage: string }[];
}

const PHASE_LABELS: Record<string, string> = {
    verifying_cluster: 'Verifying cluster identity',
    checking_missing_files: 'Checking for missing files',
    counting_files: 'Counting files',
    scanning_files: 'Scanning files',
    stopping: 'Stopping — waiting for active files to finish',
    stopped: 'Stopped early',
    completed: 'Scan finished',
    failed: 'Scan failed',
};

const STAGE_LABELS: Record<string, string> = {
    checking_record: 'Checking record',
    hashing: 'Verifying SHA-256',
    detecting_mime: 'Detecting MIME',
    moving_file: 'Moving file',
    updating_database: 'Updating database',
    parsing_metadata: 'Parsing metadata',
    storing_metadata: 'Storing metadata',
    finishing: 'Finishing',
};

export function ClusterScanProgress({progress}: { progress: ClusterScanProgressData }) {
    const total = progress.total_files;
    const finished = progress.phase === 'completed';
    return (
        <div className="job-progress">
            <span>{PHASE_LABELS[progress.phase] ?? progress.phase}</span>
            {progress.phase === 'checking_missing_files' ? (
                <span className="muted">{progress.checked_records.toLocaleString()} assigned records checked</span>
            ) : progress.phase === 'counting_files' ? (
                <span className="muted">{progress.counted_files.toLocaleString()} files counted</span>
            ) : (
                <>
                    <progress
                        aria-label="Files processed"
                        max={total === undefined ? undefined : Math.max(1, total)}
                        value={total === undefined ? undefined : total === 0 && finished ? 1 : progress.processed_files}
                    />
                    <span className="muted">
            {progress.processed_files.toLocaleString()}
                        {total === undefined ? ' files processed' : ` / ${finished ? '' : '≈'}${total.toLocaleString()} files processed`}
                        {progress.remaining_files !== undefined && ` · ${progress.remaining_files.toLocaleString()} remaining`}
                        {' · '}{progress.failed_count.toLocaleString()} failure{progress.failed_count === 1 ? '' : 's'}
          </span>
                </>
            )}
            {progress.tasks.length > 0 && (
                <details className="job-subtasks">
                    <summary>{progress.active_files} active file{progress.active_files === 1 ? '' : 's'}</summary>
                    <ul>
                        {progress.tasks.map((task) => (
                            <li key={task.task_id}>
                                <span>{STAGE_LABELS[task.stage] ?? task.stage}</span>
                                <span className="muted job-task-path" title={task.path}>{task.path}</span>
                            </li>
                        ))}
                    </ul>
                </details>
            )}
            {progress.last_failure && (
                <span className="error job-last-failure" title={progress.last_failure.path}>
          Latest failure: {progress.last_failure.error}
        </span>
            )}
        </div>
    );
}
