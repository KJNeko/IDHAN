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

## Focused checks

The `[api][cluster-scan]` cases cover scheduling after failures, early rejection
without record creation, writable orphan adoption, unchanged file-info rows,
explicit hash verification and metadata rescanning, traversal exclusions,
paginated missing-file checks, and metadata failure policy.
`[api][cluster-markers]` covers unavailable cluster identities at the API boundary.
