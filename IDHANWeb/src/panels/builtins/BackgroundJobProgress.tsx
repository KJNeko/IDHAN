export interface BackgroundJobProgressData {
    kind: 'background_service';
    service: string;
    phase: string;
    message: string;
    processed_units: number;
    failed_count: number;
    updated_at: number;
    last_error?: string;
}

const PHASES: Record<string, string> = {
    starting: 'Starting', working: 'Working', waiting: 'Waiting',
    retrying: 'Retrying', stopping: 'Stopping', stopped: 'Stopped', failed: 'Failed',
};

const UNITS: Record<string, string> = {
    hamming_distance: 'records rebuilt', phash_backfill: 'records attempted',
};

export function BackgroundJobProgress({progress}: { progress: BackgroundJobProgressData }) {
    return (
        <div className="job-progress">
            <span>{PHASES[progress.phase] ?? progress.phase}: {progress.message}</span>
            <span className="muted">
        {UNITS[progress.service] && `${progress.processed_units.toLocaleString()} ${UNITS[progress.service]} · `}
                {progress.failed_count.toLocaleString()} failure{progress.failed_count === 1 ? '' : 's'}
                {' · '}Last update: <time dateTime={new Date(progress.updated_at * 1000).toISOString()}>
          {new Date(progress.updated_at * 1000).toLocaleTimeString()}
        </time>
      </span>
            {progress.last_error &&
                <span className="error job-last-failure">Latest failure: {progress.last_error}</span>}
        </div>
    );
}
