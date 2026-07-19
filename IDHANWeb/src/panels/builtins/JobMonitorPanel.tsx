/**
 * Job Monitor — a live view of the server's in-memory jobs. Polls GET /jobs/status (no body → all jobs;
 * cleanup defaults to false, so polling never reaps a job). Jobs are process-local and reset on server
 * restart, so this reflects the current process only — it is not a durable history.
 */

import { useCallback, useEffect, useRef, useState } from 'react';
import type { PanelProps } from '../../host/types';

interface Job {
  job_id: number;
  job_name?: string;
  status?: string;
  start_time?: number; // unix seconds
  error?: string;
}

const POLL_MS = 1000;

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

function elapsed(startSeconds: number | undefined, now: number): string | null {
  if (typeof startSeconds !== 'number' || startSeconds <= 0) return null;
  const secs = Math.max(0, Math.floor(now / 1000 - startSeconds));
  if (secs < 60) return `${secs}s`;
  const mins = Math.floor(secs / 60);
  return `${mins}m ${secs % 60}s`;
}

function JobMonitorPanel({ host }: PanelProps) {
  const [jobs, setJobs] = useState<Job[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [auto, setAuto] = useState(true);
  const [now, setNow] = useState(() => Date.now());
  const timer = useRef<number | null>(null);

  const refresh = useCallback(async () => {
    try {
      const res = await host.http.fetch('/jobs/status');
      if (!res.ok) throw new Error(`/jobs/status → ${res.status}`);
      const data = (await res.json()) as { jobs?: Job[] };
      setJobs(data.jobs ?? []);
      setError(null);
      setNow(Date.now());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [host]);

  useEffect(() => {
    void refresh();
    if (!auto) return;
    timer.current = window.setInterval(() => void refresh(), POLL_MS);
    return () => {
      if (timer.current !== null) window.clearInterval(timer.current);
    };
  }, [auto, refresh]);

  return (
    <div className="panel-body job-monitor">
      <div className="job-toolbar">
        <button type="button" className="toolbar-button" onClick={() => void refresh()}>
          Refresh
        </button>
        <label className="job-auto">
          <input type="checkbox" checked={auto} onChange={(e) => setAuto(e.target.checked)} />
          Auto
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
            const since = elapsed(job.start_time, now);
            return (
              <li key={job.job_id} className="job-row">
                <span className={`job-status ${statusClass(job.status)}`}>{job.status ?? 'unknown'}</span>
                <span className="grow" title={job.error}>
                  <span className="job-name">{job.job_name ?? 'job'}</span> <span className="muted">#{job.job_id}</span>
                </span>
                {since && <span className="muted job-elapsed">{since}</span>}
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
