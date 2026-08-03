# PTR Flattener — Design

Date: 2026-07-30
Status: Approved, pending implementation plan

## Problem

Importing the Hydrus Public Tag Repository into IDHAN today replays PTR's entire published
history, edit by edit. Two consequences make a full sync impractical.

**The history is mostly superseded.** PTR publishes every mapping change ever made, so a
`(tag, file)` pair that was added, deleted, and re-added appears three times. The importer
applies all three in sequence, and the first two are wasted round trips.

**Records are touched repeatedly.** A single file that PTR tagged across two hundred update
files produces two hundred separate `addTags` calls, each carrying a sliver of that file's
tag set. Density per request is terrible and progress reporting is meaningless.

Underneath both sits a hard blocker. `PTRImportWorker` accumulates `hash_id -> hex` and
`tag_id -> tag` translation tables in RAM for the whole run
(`PTRImportWorker.hpp:119-124`). Measured against the real corpus the maximum `hash_id` is
194,644,713, so that map alone needs roughly 195M entries of `int -> std::string(64)` —
**over 20 GB resident**. The current path cannot complete a full sync regardless of how long
it is left running.

The flattener is a one-shot bootstrap tool that compacts the downloaded corpus into a
record-major form, applying the whole of PTR's history as a single collapsed result, and a
matching import path that never holds a global translation table.

## Measurements

Taken from a 120-file random sample of the real 26,324-file corpus in `~/Downloads/ptrfiles`
(20 GB compressed).

| Quantity | Value |
|---|---|
| Total update files | 26,324 |
| Content files | ~16,000 (61% of sample) |
| Mapping events per content file | 198,721 average |
| **Total mapping events** | **~3.2 billion** |
| Add / delete split | 92.5% / 7.5% |
| Max `hash_id` | 194,644,713 |
| Max `tag_id` | 47,174,184 |
| Parent + sibling pairs, whole corpus | ~186,000 |
| Free disk on target | 870 GB |

Relationships are five orders of magnitude smaller than mappings. They are a different
engineering problem and get a different, much simpler treatment.

## Decisions

| Decision | Choice |
|---|---|
| Output format | New self-contained format, not re-emitted `.ptrupdate` |
| Terminal-add chain | Collapses to one add, attributed to the **first** add's update index |
| Terminal-delete chain | Collapses to one delete at the **last** delete's index; everything before it dropped |
| Flatten scope | Mappings, parents, and siblings — all three streams |
| Output grouping | Record-major; each record appears exactly once with its final tag set |
| Incrementality | One-shot bootstrap only; the existing per-update worker handles later deltas |
| Placement | Third GUI tab; the existing Import tab gains format detection |
| Definitions in output | Per-chunk string tables, each entry carrying its PTR tag id |
| Update index in output | Dropped — used during collapse, then discarded |
| Chunk bound | `MAX_RECORDS_PER_CHUNK`, a compile-time constant |

### Why record-major

`IDHANClient::addTags` and `removeTags` (`IDHANClient.hpp:176-196`) take no timestamp. The
PTR update index therefore has nowhere to land in IDHAN, and chronological output ordering
buys nothing at import time. Emitting each record once, with its complete final tag set,
makes every `addTags` call maximally dense and guarantees no record is written twice.

### Why per-chunk string tables need the PTR tag id

Roughly 975 chunks each reference on the order of 1.5M distinct tags. Without a shared
identifier the importer would re-issue `createTags` for the same tag across many chunks —
hundreds of millions of redundant rows, plausibly costing more than the mapping import
itself. Carrying the `ptr_tag_id` alongside each string keeps chunks readable in isolation
while letting the importer hold one flat `ptr_tag_id -> TagID` array (47M x 4 B = 188 MB,
bounded, no strings on the heap) and create each tag exactly once.

The duplicated strings cost on the order of 29 GB uncompressed. Sorting each table by
`ptr_tag_id` groups same-era tags with heavy shared prefixes, so zlib should bring the
on-disk figure to roughly 8-10 GB. That is accepted: it is a one-shot job against 870 GB
free, and it buys chunks that are meaningful on their own.

## Architecture

New code lives in `tools/HydrusImporter/src/ptr/flatten/`. The collapse logic sits in a
small static library target linkable by both `HydrusImporter` and `IDHANTests`, so the pure
logic can be tested without Qt, a database, or a running server.

### Stage 1 — Scan and partition

Walk update indices in order, reusing the existing `parseUpdateFile`.

**Definitions** are written immediately into flat, id-indexed sparse files via `pwrite`. The
id *is* the offset, so no sort is required:

- `hashes.bin` — fixed 32-byte stride, binary SHA-256 rather than hex. ~6.2 GB.
- `tags.blob` + `tags.idx` — 8-byte stride index into a string blob. ~1.4 GB.

Both are later mmap'd read-only, so their cost is page cache rather than heap.

**Content mapping events** are appended as 12-byte packed records into bucket file
`hash_id % 4096`:

```
struct MappingEvent {
    u32 hash_id;
    u32 tag_id;
    u16 update_index;
    u8  op;            // add | delete
    u8  pad;
};
```

`update_index` is a `u16` because the corpus has on the order of a few thousand update
indices, well inside 65,535. This is an assumption about the data, not a guarantee, so the
scan validates it and aborts with a clear message if an index exceeds the range rather than
silently truncating. Widening it to `u32` would cost 4 more bytes per event, about 13 GB of
additional spill.

The bucket count of 4096 is purely a memory knob: it sets the working-set size of the
in-RAM sort in stage 2 and has no effect on output layout.

**Parents and siblings** accumulate in memory — at ~186k pairs this is a few MB.

Spill total: **~38 GB across 4096 buckets, ~9.3 MB each.** RAM here is dominated by the 4096
output buffers, 256 MB at 64 KB each, and that buffer size is the knob to turn if it is too
much.

### Stage 2 — Collapse per bucket

Parallel across `QThreadPool`. Each bucket is read whole into a `std::vector<MappingEvent>`
(9.3 MB), sorted by `(hash_id, tag_id, update_index, op)`, then scanned once.

This is the central trick: runs of equal `(hash_id, tag_id)` are contiguous and in
chronological order, so collapsing a chain is a purely local decision over a contiguous
span. No merge stage is needed. And because a bucket contains *every* event for each of its
records, per-record output is final the moment its span ends.

Bucketing does not produce globally sorted output, which does not matter — record-major
grouping only requires that all events for one record land together, which `hash_id % 4096`
guarantees exactly.

### Stage 3 — Emit chunks

A bucket yields only about 47,600 records (195M / 4096), so one chunk per bucket would make
`MAX_RECORDS_PER_CHUNK` unreachable and pin the chunk count to 4096 regardless of the
setting. Instead each worker pulls buckets from a shared queue and **keeps its current chunk
open across bucket boundaries**, flushing only when the cap is reached. This is safe because
every event for a record lives in exactly one bucket, so a record is never split across
chunks no matter where the boundary falls.

Each worker writes to its own chunk files, `chunk-<worker>-<n>.idhanptr`, so there is still
no cross-thread contention. Tag strings are pulled from the mmap'd `tags.blob` to build each
chunk's string table.

At the default cap this gives roughly 975 chunks in place of today's 26,324 files, and the
chunk count now scales with the cap as intended.

### Stage 4 — Relationships and manifest

Collapse the in-memory parent and sibling lists by the same rule, write
`relations.idhanptr`, then `compact_manifest.json`. Delete the bucket spills.

### Resource envelope

- **Peak RAM ~1 GB** — 256 MB of stage-1 buffers, or in stage 2 roughly 8 threads x
  (9.3 MB bucket + chunk accumulator). Mmap'd dictionaries are page cache, reclaimable
  under pressure rather than an OOM risk.
- **Peak disk ~46 GB** transient (38 GB buckets + 7.6 GB dictionaries). Buckets are deleted
  as consumed, so steady-state cost is the compacted output alone.

## Collapse rule

A pure function over one key's events, ordered by update index. Unit-tested in isolation
with no IO.

- Last op is ADD -> emit one ADD.
- Last op is DELETE -> emit one DELETE.
- Ties within one update index: DELETE sorts after ADD, matching the order
  `processSingleContentFile` applies them today. PTR does put both in one file, so this tie
  break is required for determinism.
- Repeated same-op events are idempotent and collapse identically.

The same rule applies to mappings, parents, and siblings.

Note the asymmetry between the two terminal cases. A surviving add is attributed to the
*first* add in the chain, preserving original attribution; a surviving delete takes the
*last* delete's index, which is where it actually takes effect. Since the index is dropped
from the output, this affects only collapse-time determinism and test expectations.

## On-disk format

### Chunk — `chunk-<worker>-<n>.idhanptr`

Uncompressed header, zlib body. zlib is already a dependency of the tool.

```
header (uncompressed)
  magic          char[8]   "IDHANPTC"
  version        u32       1
  body_size      u64       uncompressed body length
  record_count   u32
  string_count   u32

body (zlib)
  string table, sorted by ptr_tag_id:
    u32 ptr_tag_id, u32 len, char[len]        // no terminator
  records:
    u8[32] sha256 (binary)
    u32 add_count
    u32 del_count
    u32[add_count] str_index
    u32[del_count] str_index
```

`body_size` lets the reader allocate exactly once. Sorting the string table by `ptr_tag_id`
groups tags created in the same era, which in PTR means heavy shared prefixes
(`character:`, `creator:`, `series:`) and materially better zlib ratios.

### Relations — `relations.idhanptr`

Same header shape with its own magic. A string table, then collapsed `(a_index, b_index,
op)` triples for parents and siblings. One small file.

### Manifest — `compact_manifest.json`

Schema below; the zeroed `stats` values show shape, not defaults.

```json
{
  "format_version": 1,
  "source_update_range": [first, last],
  "max_records_per_chunk": 200000,
  "chunks": [ { "file": "chunk-0000-0.idhanptr", "records": 200000, "mappings": 3800000 } ],
  "relations_file": "relations.idhanptr",
  "stats": {
    "events_scanned": 0,
    "mappings_after_collapse": 0,
    "events_collapsed": 0,
    "terminal_deletes": 0,
    "skipped_files": 0,
    "skipped_missing_definitions": 0
  }
}
```

The manifest's presence is what marks a directory as compacted.

## Import path

`PTRImportWorker::loadMetadata` gains format detection: `compact_manifest.json` present
selects the compacted path, otherwise today's `ptr_metadata.json` then `metadata.ptrupdate`
fallback chain, unchanged. Both paths converge on the same tag-domain setup and emit the
same `PTRHistoryEntry` per unit, so `PTRImportWidget` needs no changes — one chunk simply
takes the place of one update index in the history table.

Per chunk:

1. Read and decompress.
2. Resolve the string table against the flat `ptr_tag_id -> TagID` array (188 MB, allocated
   once, zero meaning not-yet-created). Batch `createTags` for only the unseen entries and
   fill the array.
3. Batch `createRecords` over the chunk's hashes into an index-parallel `vector<RecordID>` —
   no map, since records are already in file order.
4. Batch `addTags` / `removeTags` with tag sets resolved through the array.
5. Emit a `PTRHistoryEntry`.

Importer peak RSS lands around **250 MB and stays flat** for the whole run, against the
current path's 20 GB+. Resume is chunk-granular via an `imported_chunks` set in the same
shape as today's `imported_files`.

A record whose mappings all terminal-delete still gets created, then has tags removed from
it. This matches current behaviour and keeps the two import paths equivalent.

## GUI

A third tab beside Download and Import, following the existing
`PTRDownloadWidget` / `PTRImportWorker` pattern exactly: a `QObject` + `QRunnable` worker
with `progress`, `subProgress`, `fileProcessed` and `finished` signals, a directory picker,
and a history table.

`MAX_RECORDS_PER_CHUNK` is a compile-time `constexpr` declared beside the worker's other
tunables, matching how `PTRImportWorker` declares `BATCH_SIZE`
(`PTRImportWorker.hpp:72`). It is not exposed in the UI. Default 200,000, giving roughly
975 chunks.

## Error handling

Follows the existing worker's conventions rather than inventing new ones.

- **Unparseable file in stage 1** — log and skip, as `PTRImportWorker.cpp:399-403` already
  does, but counted into the manifest as `skipped_files` so the output is honest about being
  incomplete.
- **Missing definition for a referenced id** — skip that mapping and count it, matching
  today's warn-and-skip.
- **Malformed hash** — rejected at stage 1 before it can reach `hashes.bin`, generalising
  the 64-character check at `PTRImportWorker.cpp:511`.
- **Cancellation** — checked between files in stage 1 and between buckets in stage 2. The
  manifest is written last, so a cancelled or crashed flatten leaves a directory that is not
  recognised as compacted and therefore cannot be half-imported. This is the main safety
  property of the layout.
- **Insufficient disk** — free space is checked against the ~46 GB estimate before starting,
  failing with a clear message rather than dying mid-spill after two hours.

## Testing

No existing tests cover `HydrusImporter`; the suite is server- and DB-oriented and requires
PostgreSQL. The collapse core being a separate static library is what makes the tests below
cheap to write.

**Unit, no IO, no DB:**

- `collapseChain` over hand-built sequences: `A`, `AD`, `ADA`, `ADAD`, `ADADA`, bare `D`,
  `AA`, `DD`, add and delete sharing one update index, and a single-event chain.
- Chunk writer to reader round trip, including a zero-add record and an empty string table.
- Bucket routing property: every event for a given `hash_id` lands in exactly one bucket.

**Integration, small synthetic corpus:**

- Hand-built `.ptrupdate` files with known chains; flatten and assert exact chunk contents.
- The property that matters: **flatten-then-import and raw import produce identical final
  tag state.** That is the real correctness claim, and it is cheap to assert on a synthetic
  corpus.

## Out of scope

- Incremental re-flattening. New PTR updates go through the existing per-update path.
- Windows support. Uses `pwrite`, sparse files, and `mmap`, consistent with the project's
  existing Linux-only posture for this kind of code.
- Any change to `PTRDownloader`.
- Fixing the raw import path's translation-table memory use. The compacted path avoids it;
  the raw path is left as-is for small deltas, where it is fine.
