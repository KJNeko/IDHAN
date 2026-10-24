/**
 * Job Monitor: a live view of the server's in-memory jobs. Receives snapshots and updates from
 * the shared /jobs/events stream. Jobs are process-local and reset
 * on server restart, so this is not a durable history.
 */

import {useEffect, useState} from 'react';
import {watchJobs} from '../../api/client';
import type { PanelProps } from '../../host/types';
import {ClusterScanProgress, type ClusterScanProgressData} from './ClusterScanProgress';
import {BackgroundJobProgress, type BackgroundJobProgressData} from './BackgroundJobProgress';

interface Job {
  job_id: number;
  job_name?: string;
  status?: string;
    continuous?: boolean;
  start_time?: number; // unix seconds
    end_time?: number; // unix seconds, present once the job is done
  error?: string;
    progress?: ClusterScanProgressData | BackgroundJobProgressData;
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

function JobMonitorPanel(_props: PanelProps) {
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

      {error && <p className="error">{error}</p>}
      {!error && jobs.length === 0 && <p className="muted">No jobs running.</p>}

      {jobs.length > 0 && (
        <ul className="job-items">
          {jobs.map((job) => {
              const since = elapsed(job.start_time, job.end_time, now);
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
                    {job.error && <p className="error">{job.error}</p>}
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
  description: 'Live view of the server’s running jobs.',
  component: JobMonitorPanel,
  configVersion: 1,
  singleton: true,
} as const;
