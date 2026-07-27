# Feeding modules file data via io_uring instead of mmap

## Problem

Modules receive file bytes through `ModuleCallData::file_view`, a non-owning `data_view`. Three
call sites populate that view from a memory mapping created by `FileIOUring::mmapReadOnly()`:

| Call site                                          | Purpose                              |
|----------------------------------------------------|--------------------------------------|
| `IDHANServer/src/metadata/parseMetadata.cpp:40`    | Metadata parse for a record          |
| `IDHANServer/src/api/cluster/scan.cpp:769`         | Metadata parse during a cluster scan |
| `IDHANServer/src/api/record/fetchThumbnail.cpp:95` | Thumbnail generation for a record    |

The remaining `ModuleCallData` construction sites (`api/maintenance/parseMime.cpp`,
`api/maintenance/generateThumbnail.cpp`, `modules/ModuleLoader.cpp`) build their view over an
in-memory HTTP body or over bytes a module already handed back. They never touch the disk and are
out of scope.

The mapping path has two problems worth removing:

- Module reads fault pages in synchronously on whichever thread the module runs on, invisible to
  the io_uring layer that owns every other read in the server.
- `mmapReadOnly()` signals failure by returning `{nullptr, 0}`, and **no call site checks it**. A
  failed map currently hands modules a null view.

## Approach

Read the whole file into one owned heap buffer through the existing io_uring backend before
invoking the module, and hand the module a view over that buffer. The buffer lives in a local at
the call site that outlives the module call.

Holding an entire file resident is accepted as sub-optimal. There is deliberately **no** size cap
and no fallback to a lazily-paged mapping.

## Design

### 1. `FileIOUring::readAll()`

```cpp
//! Reads the entire file into one owned buffer. Chunked because io_uring's per-SQE
//! length field is a __u32, so a single op cannot exceed 4 GiB.
[[nodiscard]] drogon::Task< std::vector< std::byte > > readAll() const;
```

Preallocate `m_size` bytes, then loop in 512 MiB blocks — the same loop shape as
`SHA256::hashCoro`, which is the established chunked-read idiom in this codebase, but with a much
larger block — `memcpy`ing each chunk into the destination at its offset.

Chunking is required, not stylistic: `IOUringLinux::read` throws when `len >= 4 GiB` because the
io_uring SQE length field is a `__u32`. A single whole-file read would hard-fail on large archives
and videos that the mapping handled.

If a chunk returns fewer bytes than requested, treat it as EOF or truncation: shrink the buffer to
the total actually read and stop.

**Single-chunk fast path.** When `m_size` fits in one block — which is every file in the collection
short of half a gigabyte — `readAll()` returns `co_await read( 0, m_size )` directly rather than
entering the loop. Combined with the move-out fix in section 2, that path allocates the buffer
exactly once and never copies it, so peak memory equals the file size.

The body touches only `read()` and `m_size`, both of which exist on every backend, so it is defined
once in a new platform-independent `IDHANServer/src/filesystem/io/IOUring.cpp` rather than
duplicated in the Linux and Windows translation units. `AddFGLExecutable` globs `src/`, so the new
file needs no CMake change.

**Accepted inefficiency:** on the multi-chunk path each chunk allocates a temporary vector that is
then copied into the destination, so peak memory for a file over 512 MiB is its size plus one 512
MiB block. Eliminating that would require a read-into-`std::span` API implemented across all three
backends (Linux io_uring, Windows IoRing, Windows IOCP). Out of scope. The fast path above keeps
this off every file below the block size.

### 2. `ReadAwaiter` correctness fixes

Two defects on the shared read path, both fixed here because `readAll()` depends on the first:

**Short reads are silently zero-padded.** `ReadAwaiter::complete( int result )` handles only
`result < 0`; a non-negative result is discarded and the buffer keeps its full requested length.
Fix: store `result` in the awaiter and, in `await_resume()`, resize `*m_data` to it when it is
`>= 0`. Shrinking a `std::vector` never reallocates, so the data pointer stays valid. The stored
result is initialised to `-1` so the never-suspended path (`await_ready()` true when `m_uring` is
null) keeps today's behaviour rather than yielding an empty buffer.

**Every read is copied.** `await_resume() const` returns `*m_data` by value, copying the entire
buffer. Fix: make it non-const and `return std::move( *m_data )`. The awaiter is single-use, so
leaving the shared buffer empty afterwards is fine.

Both mutations run on the resuming coroutine's thread, not the io thread; `complete()` only records
the integer result before queueing the resumption.

Other users of the shared path:

- `SHA256::hashCoro` — stops hashing zero-padding when a read comes back short.
- `mime::Cursor` — already tolerates a shorter-than-requested buffer through its `is_small` check
  and its `available` computation, so it only becomes more accurate.

### 3. Call site changes

Each of the three sites replaces the `mmapReadOnly()` pair-destructuring with:

```cpp
const auto buffer { co_await io->readAll() };
const idhan::data_view file_view {
    reinterpret_cast< const std::uint8_t* >( buffer.data() ), buffer.size()
};
ModuleCallData call_data { .file_view = file_view, .mime_name = mime_name, .extra = {} };
```

`ModuleCallData` itself is unchanged. Modules keep receiving a `data_view` and no module code moves.

In `parseMetadata.cpp` the read additionally moves from its current position (line 40, before the
mime lookup) to below the `findBestParser` call, so a file is not pulled into memory only for the
handler to bail out with "no parser found for mime type". The other two sites already perform their
module lookup first.

### 4. Error handling

`readAll()` propagates I/O failure by throwing, consistent with `read()` and `hashCoro`. This is a
behavioural improvement over `mmapReadOnly()`'s unchecked `{nullptr, 0}`, but it means each call
site must now catch and convert:

- `parseMetadata.cpp` and `fetchThumbnail.cpp` — convert to `createInternalError` and return it as
  the handler's error response.
- `scan.cpp` — same, and it matters most here: this code runs inside a job coroutine, where an
  escaping exception would take down the job rather than surface as a per-record failure.

### 5. Removal of `mmapReadOnly()`

With the three call sites converted, nothing uses it. Delete:

- The declaration in `IDHANServer/src/filesystem/io/IOUring.hpp`, plus the `m_mmap_ptr`
  (`std::shared_ptr< void >`, Linux) and `mutable m_mmap_buffer` (`std::vector< std::byte >`,
  Windows) members it exists to manage.
- The definition and its munmap deleter in `IDHANServer/src/filesystem/io/linux/IOUringLinux.cpp`.
- The definition in `IDHANServer/src/filesystem/io/windows/IOUringWindows.cpp`.

`<sys/mman.h>` stays included on Linux — the io_uring ring setup still maps the submission and
completion rings.

## Testing

There is no existing coverage of the IO layer under `tests/`. The short-read resize is the piece
most worth pinning down, but exercising it requires provoking a genuinely short read, which the
current fixtures have no way to set up. Verification for this change is a build plus a manual
thumbnail-generation and cluster-scan run. Building a fixture that can produce a truncated read is
deferred; if it is wanted, it should be its own piece of work.

## Out of scope

- A read-into-`std::span` backend API to eliminate the per-chunk copy.
- Any size cap, memory budget, or concurrency limit on how many whole files may be resident at
  once. A parallel cluster scan can hold N whole files in memory simultaneously; this is a known
  and accepted consequence.
- Changes to `ModuleCallData` or to any module.
- The three in-memory `ModuleCallData` sites that never touch the disk.
