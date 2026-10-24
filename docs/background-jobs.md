# Background service jobs

Clients can discover automatically started services through `GET /jobs/status`
and poll individual jobs through `GET /jobs/{job_id}/status`. Each service owns
a job with `progress.kind = "background_service"`; recurring maintenance has `continuous = true`, while startup
perceptual hash backfill has `continuous = false`. Its progress
follows `BackgroundJobProgress` in the API specification. The Job Monitor renders
the Continuous badge, current activity, counters, latest error, and last activity time.
Finite jobs such as cluster scans have `continuous = false`.

| Stable `progress.service` | Service                          | Start and activity                                                                                 |
|---------------------------|----------------------------------|----------------------------------------------------------------------------------------------------|
| `hamming_distance`        | Hamming distance maintenance     | Starts at boot; waits one second after processing work and five minutes when no work is available. |
| `phash_backfill`          | Missing perceptual hash backfill | Runs once at boot, then completes.                                                                 |
| `module_workers`          | Module worker maintenance        | Starts at boot; checks workers and retires idle processes every 30 seconds.                        |

The job remains `running` while its phase is `waiting` or `retrying`.
Waiting describes an active service with no current operation.
Hamming job progress includes the next sweep's wait time. New records queued while
the worker is idle may wait up to five minutes; processing work or encountering an
error resets the delay to one second. Shutdown interrupts the wait immediately.

Recoverable errors increment `failed_count` and retain `last_error` after recovery.
Hamming sweeps retry after one second; module maintenance retries on its existing
timer. Perceptual hash backfill runs once at startup. These continuous services have
no finite total or ETA. The continuous flag describes their lifecycle independently
of their current activity and remains true after they stop.

Client heartbeats and the downloader manager run as infrastructure without job
registrations. Download sessions provide feedback on actual transfers; the manager
itself only owns worker pools and submission routing, so it has no separate job row.

Counters are cumulative within one process. Hamming processed units count records
in successfully completed sweeps; a sweep that fails after earlier batches commit
can undercount those earlier batches. Perceptual hash units count attempted records,
including failures and successful parses producing no hash. A later pass may attempt
the same record again. `updated_at`
changes when activity or counters change, rather than on each status poll.

Cluster configuration comes from the shared `ClusterManager` cache documented in
[cluster-cache.md](cluster-cache.md). Perceptual hash backfill reuses pinned access
within each eight-record batch, with marker checks before reads and hash updates.
Configuration changes commit and publish through the shared cache; batch access
is owned by the sequential coordinator and does not escape its lifetime.

Jobs and IDs are process-local. IDs are assigned anew at restart and may reuse old
values; identify services by `progress.service`. Running jobs survive cleanup
requests and retention sweeps. After a service drains, its job completes normally
and follows ordinary job retention. A terminal error reported to the job marks it
failed. This is service activity reporting, not a watchdog for a hung operation;
an unchanged activity timestamp helps clients recognize work that is taking time.

`jobs::BackgroundJob` queues a coroutine that publishes a synchronized progress
provider and suspends on a Folly baton. The service keeps its existing executor
and dependencies; waiting does not consume a job-pool thread. The owner finishes
the job only after draining its work, and posting completion happens outside the
progress mutex because it can resume the coroutine inline. The job frame owns
the provider so final snapshots remain safe after the service object is destroyed.

`HashMaintenance` and `ModuleMaintenance` own these registrations. Hash workers drain while module
maintenance is still alive; all finish before their dependencies are destroyed.
The job runtime's retention timer wakes immediately during shutdown. Internal I/O,
module IPC, logging, and executor threads remain implementation details of their
owning services rather than separate user jobs.
