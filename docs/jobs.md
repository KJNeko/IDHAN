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
carry `records` to scope the job to those records; without it every record with file info is
rescanned. The stored result reports `scanned_count` and `failed_count`.

## Test Job

Dispatched by `GET /test`. A trivial job (runs a `SELECT 1`) used to exercise the job machinery.

# Periodic maintenance

`ServerContext` owns separate missing-image-phash and hamming-distance workers. Each starts a
sweep at startup, waits one second after it completes or fails, then starts another. A worker has
at most one sweep in flight. Its coroutines run on the native database executor; its owned timer
thread waits for completion without blocking that executor. These sweeps are not job-endpoint jobs.

A sweep remembers the highest eligible record ID and advances through ordered batches up to that
ID. It does not load the entire pending list into memory. New work behind its cursor, newly
eligible records, and skipped locked queue rows are picked up by a later sweep. Each sweep is
finite even while new records continue arriving.

Generation reads up to eight image rows at a time with a NULL phash, a cluster assignment, and
no delete timestamp. Each attempt opens the original through verified cluster access, uses the
existing remote module metadata parser, and verifies the cluster again after parsing. Its
conditional write changes only a still-NULL phash, checking the active cluster assignment and
MIME again in the update. It preserves other metadata and a foreground parser's existing hash.
Missing files do not detach file assignments; marker and parse failures are logged. Failed parses,
unavailable parsers, and blank results cannot stop later IDs progressing, and remain eligible
for a later sweep. Backfill does not discover records with no image-metadata row.

Distance batches claim at most 32 queue rows using `FOR UPDATE SKIP LOCKED`. They acquire the
existing relationship advisory lock `(16609, 1)` first, then delete the claimed queue rows, remove
old pairs touching those records, and rebuild pairs within eight bits in one transaction.
`duplicate_members` and the existing alternative/unrelated tables exclude ruled pairs and pairs
within a duplicate group. Cleared hashes remove obsolete pairs without generating new ones.
On failure, rollback preserves both pending work and the previous pairs. A concurrent hash update
that encounters a claimed queue row waits for its deletion transaction and then leaves a fresh
queue entry; its newer change is processed by a later batch or sweep.

The eight-bit pair-cache cutoff is independent of the zero-distance default for direct similarity
search. Rebuilding uses exact comparisons against non-NULL hashes; a bounded queue batch does not
make that comparison cost independent of collection size. No approximate candidate limit is used.

Shutdown requests both workers to stop, wakes their timer waits, and joins them before destroying
their database, executor, cluster manager, or module loader. A distance transaction already in
flight finishes atomically. Backfill stops between records and checks shutdown after a remote
parse before writing. An in-flight module call must return before its worker can drain; shutdown
does not add a new forced cancellation policy to the module protocol. Pending database work
survives a crash and is reconsidered at the next startup.

# Job status

The status endpoints return a JSON object per job. `status` is one of:

- `dispatched` — returned by the dispatching endpoint the moment a job is queued
- `running` — the job coroutine has started and has not finished
- `completed` — the job finished successfully; an optional `response` field carries its stored result
- `failed` — the job finished with an error; an `error` field carries the message
- Unknown or cleaned-up IDs return HTTP 404 from the single-job endpoint. The serializer also supports an internal
  `not_found` placeholder.

Other fields include `job_id`, `job_name`, `location` (the source location that dispatched it), and,
once started, `start_time`.

Inside a job coroutine, `co_await getJobID()` yields the job's id and
`co_await setJobResponse(json_or_response)` stores the result later exposed as `response`.

# Internals

`JobRuntime` (`getJobRuntime()`, a process-wide singleton) owns an in-memory queue and two threads:

- a **runner** thread that pops queued jobs and dispatches each onto a `folly::CPUThreadPoolExecutor`,
- a **cleanup** thread that reaps finished jobs.

The pool size comes from `server.job_threads` (config), defaulting to
`hardware_concurrency / 4`, minimum 2.

`queueJob(task, name)` enqueues a `JobTask` coroutine, wakes the runner, and returns a `JobContext`
(shared pointer) whose `id()` is the job id handed back to the client. Completed jobs are retained
for one hour so their result stays pollable, or dropped sooner once a status query has read them.

There is no dependency graph, no `prepare()` phase, and no pause/resume: a job runs to completion (or
failure) once dispatched.
