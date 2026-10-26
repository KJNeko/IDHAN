/**
 * Job Monitor starts manual maintenance jobs and follows the server's in-memory jobs
 * through the shared /jobs/events stream. Jobs are process-local and reset
 * on server restart, so this is not a durable history.
 */

import {useEffect, useState} from 'react';
import {watchJobs} from '../../api/client';
import {UrlReorganizationProgress, type UrlReorganizationProgressData} from './UrlReorganizationProgress';
import {JobLauncher} from './JobLauncher';
import type { PanelProps } from '../../host/types';
import {ClusterScanProgress, type ClusterScanProgressData} from './ClusterScanProgress';
import {BackgroundJobProgress, type BackgroundJobProgressData} from './BackgroundJobProgress';

interface UrlReorganizationResult {
    kind: 'url_reorganization';
    total_count: number;
    unique_count: number;
    merged_count: number;
    sorted_count: number;
}

interface MetadataRescanResult {
    scanned_count: number;
    failed_count: number
}

interface EmbeddingResult {
    model_name: string;
    embedded: number;
    failed: number;
    skipped: number;
    error?: string
}

interface Job {
  job_id: number;
  job_name?: string;
  status?: string;
    continuous?: boolean;
  start_time?: number; // unix seconds
    end_time?: number; // unix seconds, present once the job is done
  error?: string;
    response?: UrlReorganizationResult | MetadataRescanResult | EmbeddingResult;
    progress?: ClusterScanProgressData | BackgroundJobProgressData | UrlReorganizationProgressData;
}

function statusClass(status: string | undefined): string {
  switch (status) {
    case 'completed':
      return 'ok';
    case 'failed':
      return 'error';
    case 'running':
      return 'running';
    default:
      return 'muted';
  }
}

function elapsed(startSeconds: number | undefined, endSeconds: number | undefined, now: number): string | null {
  if (typeof startSeconds !== 'number' || startSeconds <= 0) return null;
    const until = typeof endSeconds === 'number' ? endSeconds : now / 1000;
    const secs = Math.max(0, Math.floor(until - startSeconds));
  if (secs < 60) return `${secs}s`;
  const mins = Math.floor(secs / 60);
  return `${mins}m ${secs % 60}s`;
}

function JobMonitorPanel({host}: PanelProps) {
  const [jobs, setJobs] = useState<Job[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [auto, setAuto] = useState(true);
  const [now, setNow] = useState(() => Date.now());

    useEffect(() => {
        if (!auto) return;
        return watchJobs({
            onJobs: (updated) => {
                setJobs(updated as Job[]);
                setError(null);
                setNow(Date.now());
            },
            onError: setError,
        });
    }, [auto]);

    // Elapsed time advances locally; it does not require another server request.
  useEffect(() => {
      const timer = window.setInterval(() => setNow(Date.now()), 1000);
      return () => window.clearInterval(timer);
  }, []);

  return (
    <div className="panel-body job-monitor">
      <div className="job-toolbar">
        <label className="job-auto">
          <input type="checkbox" checked={auto} onChange={(e) => setAuto(e.target.checked)} />
            Live
        </label>
        <span className="muted grow">
          {jobs.length} job{jobs.length === 1 ? '' : 's'}
        </span>
      </div>

        <JobLauncher host={host} jobs={jobs} onQueued={job => setJobs(current =>
            current.some(existing => existing.job_id === job.job_id) ? current : [...current, job])}/>
      {error && <p className="error">{error}</p>}
      {!error && jobs.length === 0 && <p className="muted">No jobs running.</p>}

      {jobs.length > 0 && (
        <ul className="job-items">
          {jobs.map((job) => {
              const since = elapsed(job.start_time, job.end_time, now);
              const response = job.response && typeof job.response === 'object' ? job.response : undefined;
            return (
                <li key={job.job_id} className="job-item">
                    <div className="job-row">
                        <span className={`job-status ${statusClass(job.status)}`}>{job.status ?? 'unknown'}</span>
                        {job.continuous && <span className="job-status job-continuous">Continuous</span>}
                        <span className="grow" title={job.error}>
                    <span className="job-name">{job.job_name ?? 'job'}</span> <span
                            className="muted">#{job.job_id}</span>
                  </span>
                        {since && <span className="muted job-elapsed">{since}</span>}
                    </div>
                    {job.progress?.kind !== 'url_reorganization' && response && 'kind' in response && response.kind === 'url_reorganization' && (
                        <p>Total: {response.total_count.toLocaleString()} ·
                            Unique: {response.unique_count.toLocaleString()} ·
                            Merged: {response.merged_count.toLocaleString()} ·
                            Sorted: {response.sorted_count.toLocaleString()}</p>
                    )}
                    {response && 'scanned_count' in response && <p>
                        Scanned: {response.scanned_count.toLocaleString()} ·
                        Failed: {response.failed_count.toLocaleString()}
                    </p>}
                    {response && 'embedded' in response && <p>
                        Embedded: {response.embedded.toLocaleString()} · Failed: {response.failed.toLocaleString()} ·
                        Skipped: {response.skipped.toLocaleString()}
                    </p>}
                    {response && 'error' in response && response.error && <p className="error">{response.error}</p>}
                    {job.error && <p className="error">{job.error}</p>}
                    {job.progress?.kind === 'url_reorganization' &&
                        <UrlReorganizationProgress progress={job.progress}/>}
                    {job.progress?.kind === 'cluster_scan' && <ClusterScanProgress progress={job.progress}/>}
                    {job.progress?.kind === 'background_service' && <BackgroundJobProgress progress={job.progress}/>}
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}

export const jobMonitorPanel = {
  type: 'job-monitor',
  title: 'Job Monitor',
    description: 'Start maintenance jobs and follow their progress and results.',
  component: JobMonitorPanel,
  configVersion: 1,
  singleton: true,
} as const;
