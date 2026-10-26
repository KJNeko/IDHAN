# Cluster identity markers and acknowledged recovery

Implemented on 2026-10-20. Each cluster root contains an IDHAN-owned
`.idhan-cluster.json`. Migration 416 adds a unique, non-null `cluster_uuid` to
`file_clusters`; this UUID identifies the storage root independently of the
small integer cluster ID or its configured path.

## Marker format

```json
{
  "format": "idhan.cluster",
  "version": 1,
  "cluster_uuid": "723963ce-7749-4dd4-82d8-a6f14881e709",
  "created_at": 1792454400,
  "metadata": {}
}
```

`created_at` is the marker creation time in Unix seconds. `metadata` is an
extensible object. Additional fields are accepted and preserved: an existing
valid marker is read without rewriting it. Names, quotas, ratios, and read-only
settings remain database configuration. Renaming a cluster retains its UUID.

The reader rejects malformed JSON, duplicate keys, unsupported versions,
invalid or nil UUIDs, invalid field types, marker symlinks, and non-regular
files. Missing and conflicting markers have different recovery policies.

## Registration and uniqueness

Registration automatically creates a marker if none exists, including for a
cluster configured `readonly`. Read-only protects media files; initializing
cluster identity is an explicit exception. The directory must permit this write
at the filesystem level. An existing valid marker is reused, allowing a failed
registration to be retried or a removed registration to be added again.

Exact names and absolute, lexically normalized paths remain unique. Physical
directory equivalence detects aliases such as symlinks and bind mounts. The
unique database UUID constraint also rejects a copied directory carrying an
already registered identity. Copying a marker does not create an independent
cluster. These checks concern this database; markers are not a cross-server
locking or ownership protocol.

Marker creation writes and synchronizes a temporary file, installs it atomically
without replacement, then reads the winning marker. Concurrent creators cannot
overwrite each other's identity. IDHAN never replaces an existing invalid
marker. Registration failures can leave a valid marker behind for a retry.
Cluster removal deletes the registration and retains both marker and media.

## Startup and recovery

Startup verifies every registered root against its expected database UUID before
serving requests or resuming work. A missing directory or marker, invalid marker,
or UUID mismatch stops startup with cluster-specific diagnostics. This also
applies to existing installations upgrading from a version without markers.

After checking that the configured paths point to the intended storage, set:

```toml
[cluster]
recreate_missing_markers = true
```

On the next startup, IDHAN first persists this setting back to `false` in the
same TOML file, then attempts recovery. Resetting preserves the surrounding
configuration text, comments, ordinary file permissions, and ownership. The
configuration file and its directory must allow atomic replacement; a symlink
configuration file is refused for this operation. If the reset cannot be
persisted, no recovery changes are attempted. True CLI or environment overrides
are rejected because IDHAN cannot persistently reset those sources.

Acknowledged recovery creates missing root directories and missing markers using
the UUID already stored in the database. It also applies to logically read-only
clusters. It never overwrites malformed, unsupported, or mismatched markers.
Verification must still pass for every cluster before the server starts.
The flag is consumed even when a later recovery or verification step fails;
repair the cause and set it again to retry. Recovery can complete some missing
markers before discovering a different cluster's failure.

This acknowledgement can be used again after a later folder wipe. There is no
permanent adoption state. Recovery restores directory identity, not lost media.
The operator must ensure an expected volume is mounted: creating a marker in an
empty mount point cannot prove that it is the intended disk. A surviving valid
marker is authoritative and is never replaced by recovery.

## Verification during operation

Imports, original downloads, metadata parsing, thumbnail generation from original
media, embeddings, and scans open their cluster through verified access.
Missing or mismatched identity makes cluster access unavailable. HTTP file work
returns 503; background work reports or logs failure. Cached thumbnails and
configuration inspection need no cluster media access. Removing a registration
remains available for recovery. Identity failure is never treated as an ordinary
missing file: it must not detach record assignments or silently redirect an
existing record to another cluster.

Verified access owns an open root directory descriptor. Linux paths through
`/proc/self/fd` keep subsequent opens tied to that root while the owner remains
alive; module inputs then own their file descriptors. Replacing or remounting
the configured path cannot redirect an operation already holding the old root.
Long-running scans and metadata work recheck the marker before later mutations.
The root marker is outside scan media buckets and is not imported or deleted.

This protects root identity, not arbitrary manipulation inside media buckets.
It is not a filesystem sandbox or a lock against another process changing a
marker immediately after validation. A wiped root's original metadata cannot
be reconstructed from its UUID; a recreated marker has a new creation time and
empty metadata.

## Verification

Permanent unit tests cover atomic creation, racing creators, extensible fields,
invalid markers and symlinks, root-handle lifetime, and persisted flag reset.
API regressions cover read-only registration, identity reuse, physical aliases,
copied identities, unavailable file/import/scan access, and retained record
assignments. A separate startup integration exercise covers initial upgrade,
missing markers, complete root loss, acknowledged recovery, conflicting UUIDs,
and failure to persist the reset across ten server launches.
