# File scanning and import

Cluster scans and initial imports share record creation, assigned-path resolution,
streaming SHA-256 hashing, and metadata parsing and persistence. Their entry points
retain different policies: imports can succeed when metadata parsing fails, while
cluster scans report that file as failed.

## Code map

| Responsibility                              | Location                                      |
|---------------------------------------------|-----------------------------------------------|
| HTTP parameters and option dependencies     | `IDHANServer/src/httpapi/clusters.cpp`        |
| Normalize scan modes and publish job JSON   | `IDHANServer/src/httpapi/clusterScan.cpp`     |
| Directory traversal and candidate ownership | `IDHANServer/src/scanning/ScanDirectory.*`    |
| Plan and process files; schedule workers    | `IDHANServer/src/scanning/clusterScan.cpp`    |
| Live phases, counters and active file tasks | `IDHANServer/src/scanning/ScanProgress.*`     |
| Record and file-info queries                | `IDHANServer/src/db/records.*`, `fileInfo.*`  |
| Resolve and pin a record's assigned cluster | `IDHANServer/src/filesystem/recordFilePath.*` |
| Hash files with bounded memory              | `IDHANServer/src/filesystem/hashFile.*`       |
| Parse metadata and commit its results       | `IDHANServer/src/metadata/RecordMetadata.*`   |
| Initial import decisions and storage        | `IDHANServer/src/import/ImportFile.cpp`       |

## Per-file flow

1. Reject empty files, sizes the database cannot represent, and invalid hash names.
2. Reject misplaced files immediately when physical changes are prohibited.
3. Verify cluster identity and load record state, including metadata presence.
4. Reject unknown records unless adoption is enabled. Check an existing assignment
   before doing expensive reads.
5. Build a plan from the options and observed state. MIME and metadata modes are
   `Disabled`, `MissingOnly`, or `Always`. Hash verification is mandatory for new
   orphan records.
6. Hash and detect MIME as requested. Determine the final extension and destination
   before moving the file, so directory and extension repairs take one move.
7. Inspect file times, verify identity again, and create a record if needed.
8. Persist file information only when its stored fields differ. Parse metadata
   when the plan requires it and a MIME is available.

Matching file times and size can avoid a redundant database update. They do not
establish content integrity and never suppress requested hashing or rescanning.
Parsing happens outside the metadata transaction. Metadata and its file times
commit together after identity verification. A metadata failure can therefore
leave successfully updated file information, matching the existing scan policy.
Active file tasks report `parsing_metadata` while the module parses the file, then
`storing_metadata` while verifying identity, acquiring a transaction, writing the
metadata and file times, and committing. The Job Monitor labels these separately
as "Parsing metadata" and "Storing metadata" so database waits are visible.

Larger static queries live in an `sql/` directory next to their owning C++ source
and are compiled into null-terminated character arrays with C++26 `#embed`.
The SQL files document their positional parameters; binding and result decoding
remain in the typed C++ helpers. Simple queries remain inline. SQL edits rebuild
their owning source through compiler dependency tracking, and CMake lists the
SQL files as resources for IDE navigation. Running the server requires no query
files on disk.

## Ownership and concurrency

The scan owns its options, pinned `ClusterAccess`, completion queue, stop flag, and
worker scope until every scheduled worker has finished. The coordinator alone
advances directory iterators and updates totals and the bounded failure list.
Workers own their candidates and local results, and publish outcomes through a
Folly bounded queue. The shared stop flag is atomic.

The coordinator admits at most `concurrency` files. It admits a replacement when
an outcome arrives, without waiting for the slowest file in a batch. A worker
publishes failure to the stop flag before publishing its outcome when
`stop_on_fail` is enabled. Once the stop is observed, the coordinator admits no
more files. Already scheduled files finish and remain included in the totals.
Cluster identity failures also stop further scheduling. Missing-file preflight
runs before workers start and reads assigned records in bounded pages.

Helpers that borrow `ClusterAccess` require it to remain alive across coroutine
suspension. Its `/proc/self/fd` paths depend on the pinned directory descriptor;
they must not escape that lifetime. Module calls own their input descriptors
through `CallInput`. Each metadata operation acquires its own transaction.

There is no per-record serialization between jobs. Assignment updates retain
their existing last-write-wins behavior. Moves still refuse to overwrite an
existing destination, and a file already present in another assigned cluster
remains a reported conflict.

For future concurrency changes, preserve the ownership boundaries and keep disk
reads, module capacity, and database connection limits in mind. Increasing the
number of file workers alone may not increase throughput.

## Live job feedback

Both job-status endpoints expose an optional `progress` snapshot, separate from
the unchanged final `response`. Cluster scans report identity verification,
missing-file preflight, file counting, processing, draining after failure, and
completion or early stop. The Job Monitor displays these phases, processed and
remaining counts, failures, and expandable active file subtasks with their paths
and current steps. Subtasks have IDs local to their parent job; they do not
create queued jobs or retain a per-file history.

After preflight, the scanner makes a directory-and-size counting pass using the
same traversal filters as processing. It does not hash or parse files or retain
all paths. This adds filesystem reads before processing. Totals are estimates
while files can change; new admissions raise the total when necessary, and normal
completion reconciles it to the processed count. Stop-on-fail leaves the estimated
unfinished count visible after draining. If traversal errors prevent counting,
the total is omitted and processing reports those errors. Counting itself stops
at its first traversal error when stop-on-fail is enabled.

`ScanProgress` protects its counters and bounded task map with a mutex so workers
can update their own stages safely. Job status holds a shared progress provider;
it requests JSON only when polled, rather than serializing on every file update.
The generic job response is also synchronized for jobs that publish partial results.
Snapshots are consistent individually but the job envelope and progress are not
one atomic transaction. Progress survives job completion until normal job cleanup;
it resets with the process. Processed bytes are file sizes, including failed attempts,
rather than measured disk I/O. Missing-file preflight reports checked records without
adding a separate database count query.

## Focused checks

The `[api][cluster-scan]` cases cover scheduling after failures, early rejection
without record creation, writable orphan adoption, unchanged file-info rows,
explicit hash verification and metadata rescanning, traversal exclusions,
paginated missing-file checks, and metadata failure policy.
Progress checks hold record lookups with a database lock to verify bounded live
subtasks deterministically, then verify completion, empty totals, and early stops.
`[api][cluster-markers]` covers unavailable cluster identities at the API boundary.

Cluster configuration reads and mutations use the shared cache described in
[cluster-cache.md](cluster-cache.md). Usage counters remain live.
