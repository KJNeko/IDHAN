# Jobs

Long-running work (cluster scans, metadata rescans) is broken into coroutine **jobs** run off the
request thread. A job is dispatched immediately, the endpoint responds with a `job_id`, and the
client polls a status endpoint until the job completes.

Jobs are **not persisted to the database**. IDs are process-local and reset on restart; there is no
`jobs` table. Everything below is in-memory state owned by the `JobRuntime` singleton
(`IDHANServer/src/jobs/`).

# Job endpoints

- `GET /jobs/status` — status of all currently tracked jobs
- `GET /jobs/{job_id}/status` — status of a single job
- `POST /jobs/metadata/rescan` — dispatch a metadata rescan job

Cluster scans are dispatched through the cluster API rather than a `/jobs` route:

- `POST /clusters/{cluster_id}/scan` — dispatch a scan of the given cluster

# Job types

Anything queued via `queueJob()` is a job. The current job types are:

## Cluster Scan

Dispatched by `POST /clusters/{cluster_id}/scan`. Scans a cluster's directory for files that are
present, missing, or corrupted, and indexes what it finds. While the cluster is read-only the scan
never modifies files.

## Metadata Rescan

Dispatched by `POST /jobs/metadata/rescan`. Re-runs metadata extraction over records. The body may
carry `record_ids` to scope the job to those records; without it every record with file info is
rescanned. The stored result reports `scanned_count` and `failed_count`.

## Test Job

Dispatched by `GET /test`. A trivial job (runs a `SELECT 1`) used to exercise the job machinery.

# Job status

The status endpoints return a JSON object per job. `status` is one of:

- `dispatched` — returned by the dispatching endpoint the moment a job is queued
- `running` — the job coroutine has started and has not finished
- `completed` — the job finished successfully; a `response` field carries its stored result
- `failed` — the job finished with an error; an `error` field carries the message
- `not_found` — no job with that id is currently tracked (never existed, or already cleaned up)

Other fields include `job_id`, `job_name`, `location` (the source location that dispatched it), and,
once started, `start_time`.

Inside a job coroutine, `co_await getJobID()` yields the job's id and
`co_await setJobResponse(json_or_response)` stores the result later exposed as `response`.

# Internals

`JobRuntime` (`getJobRuntime()`, a process-wide singleton) owns an in-memory queue and two threads:

- a **runner** thread that pops queued jobs and dispatches each onto a `trantor::EventLoopThreadPool`,
- a **cleanup** thread that reaps finished jobs.

The pool size comes from `server.job_threads` (config), defaulting to
`hardware_concurrency / 4`, minimum 2.

`queueJob(task, name)` enqueues a `JobTask` coroutine, wakes the runner, and returns a `JobContext`
(shared pointer) whose `id()` is the job id handed back to the client. Completed jobs are retained
for one hour so their result stays pollable, or dropped sooner once a status query has read them.

There is no dependency graph, no `prepare()` phase, and no pause/resume: a job runs to completion (or
failure) once dispatched.
