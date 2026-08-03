# Module sandbox: capability-scoped file I/O

**Date:** 2026-08-02
**Status:** Draft, pending review
**Revised:** 2026-08-02, twice.

1. `scm_probe` disproved the original transport — an io_uring descriptor cannot cross a unix socket.
2. **io_uring was then dropped entirely** in favour of handing the worker a shared mapping of the
   record file. The ring bought one thing a mapping cannot — hiding the file's path while also
   avoiding a copy — at a cost in machinery that was no longer worth it once the sandbox was going
   to close the path anyway. The investigation is preserved in Appendix A, because it is the
   evidence for why an obvious-looking approach does not work.

## 1. Motivation

### 1.1 Threat model

The adversary is **a crafted file that exploits a decoder**. A malicious image, video or archive
triggers a memory-safety bug in libvips, FFmpeg or libarchive and obtains arbitrary code execution
inside the module worker process. The module code is ours and is trusted; the attacker's foothold is
the third-party decoder, and they obtain it *during* a call, after the library is loaded and
initialised.

Everything below follows from that. Containment applied before the first call is serving its
purpose; containment that would have to be applied mid-call is not.

### 1.2 What exists today

Modules already run out-of-process (`IDHANModuleRunner`, one per library), which contains crashes,
hangs and leaks. Two things are missing.

**There is no confinement.** `WorkerProcess::start()` (`IDHANServer/src/modules/WorkerProcess.cpp:102`)
is a plain `fork()` + `execl()`. No `close_range`, no `PR_SET_NO_NEW_PRIVS`, no `PR_SET_PDEATHSIG`,
no rlimits. Every descriptor in the server that is not `FD_CLOEXEC` — including whatever libpq holds
— is inherited straight into the worker. §6.1 of the worker-framework spec called for `PDEATHSIG`
and it was never implemented, so workers currently outlive a `SIGKILL`ed server.

**Every call copies the whole file, twice over.** `filesystem::mapRecordBlob`
(`IDHANServer/src/filesystem/getIOForRecord.cpp:30`) `copy_file_range`s the record into an anonymous
memfd, and the module receives a flat `data_view` over the mapping. A 4 GiB video thumbnail
therefore allocates 4 GiB of tmpfs alongside the file's own page cache, in order to decode one frame
at the 10% mark. The return path is worse: `GeneratorModuleI::generate` returns
`std::expected< std::vector< std::byte >, ModuleError >`, so extracting a 500 MB archive member
materialises it in the worker's heap and then copies it again into a memfd to cross back.

The memfd exists for a good reason — it hides the cluster path, which a real file descriptor would
leak through `readlink("/proc/self/fd/N")`. Any replacement has to preserve that, and a registered
ring slot does it better: an index is not a descriptor, so there is nothing in `/proc/self/fd` to
read at all.

What the memfd does *not* solve is the memory. Its pages are tmpfs pages, resident for the file's
full size for the whole call, charged to the server. "Not in the heap" is not "not in memory", and
one in-flight call per large file is one full copy of that file in RAM. Mapping the file itself
(§6) removes that copy; hiding the path becomes the sandbox's job instead of the transport's.

### 1.3 What this design does

Hand the worker a **read-only shared mapping of the record file itself**, and let modules read ranges
out of it. The server sends the file's `O_RDONLY` descriptor over the existing channel; the worker
maps it and closes the descriptor. Nothing is copied, and nothing is resident that a module has not
actually touched — a thumbnailer decoding one frame near the start of a 4 GiB video faults in a few
megabytes of page cache and nothing more.

Modules gain range reads (so they stop materialising whole files) and a write sink (so results are
built once, in the shared memory that carries them back).

**The one thing this does not do by itself is hide the path.** A file-backed mapping appears in
`/proc/self/maps` with the file's name, so a worker that can read `/proc` can recover the cluster
path. That is closed by the sandbox — Landlock or a mount namespace denying `/proc` — which makes
§10's confinement a **dependency of this design**, not an optional follow-up. Appendix A records the
alternative that avoided the leak without a sandbox, and why it was not worth its cost.

The two properties are not simultaneously obtainable from a mapping: a mapping needs a backing
object the worker can name, and naming it is the leak, while making it anonymous is the copy. Only
an io_uring registered slot — an index with no descriptor and no `maps` entry — is both, which is
what the ring was for.

## 2. Decisions taken

| Decision | Choice |
|---|---|
| Threat model | Crafted file exploits a codec; module code trusted |
| Input transport | A read-only shared mapping of the record file, sent as a descriptor |
| Module ABI | Replace `file_view` outright; all premade modules convert together |
| Convenience `readAll()` | **Not provided.** Modules own every buffer |
| Return path | In scope — a sink, so large outputs are held once, not twice |
| Whole-file copy | Removed. The server maps the file; it never duplicates it |
| Path hiding | Delegated to the sandbox (`/proc` denial), not to the transport |
| io_uring | **Rejected.** See Appendix A |
| Containment in this spec | fd hygiene, `NO_NEW_PRIVS`, `PDEATHSIG`, `RLIMIT_CORE`, `PR_SET_DUMPABLE` |
| Landlock / seccomp policy | Follow-up spec, but now a **dependency** rather than a nicety |

## 3. `ModuleFile` — the read ABI

New header `IDHANModules/include/ModuleFile.hpp`, replacing `data_view` in `ModuleCallData`.

```cpp
class FGL_EXPORT ModuleFile
{
  public:
    virtual ~ModuleFile() = default;

    [[nodiscard]] virtual std::size_t size() const = 0;

    //! Reads into the caller's buffer at the given offset. Returns how many bytes were written to
    //! out -- short only at EOF.
    /** Every byte of storage involved belongs to the caller. There is deliberately no readAll():
     *  a module's peak memory should be what that module chose to allocate, visible at the site
     *  that chose it, and not something the host added on its behalf. A module that genuinely
     *  needs the whole file contiguous allocates it itself, in its own code, where it can be
     *  seen in review. */
    [[nodiscard]] virtual std::expected< std::size_t, ModuleError >
        read( std::span< std::byte > out, std::size_t offset ) const = 0;

    //! Wraps caller-owned memory, for bytes a module produced itself and wants to pass back
    //! through a callback. Wraps -- it does not copy.
    [[nodiscard]] static std::unique_ptr< ModuleFile > fromBytes( std::span< const std::byte > bytes );
};

struct ModuleCallData
{
    const ModuleFile& file;
    std::string mime_name;
    Json::Value extra;
};
```

`ModuleBase::estimateDuration` keeps working unchanged in shape: `data.file.size()` replaces
`data.file_view.size()`, and the size is known from the call frame without reading a byte.

Dropping `readAll()` is what makes three of the four backends allocate nothing extra, because the
consuming library already owns the buffer it wants filled — see §9.

## 4. `ModuleSink` — the write ABI

Generator output is inherently large and this design does not try to make it smaller. It ensures it
exists **once**: the module writes directly into the shared memory that will carry the result back,
instead of filling a heap vector that is then copied into a memfd.

```cpp
class FGL_EXPORT ModuleSink
{
  public:
    virtual ~ModuleSink() = default;

    //! Declares the final size up front, when the module knows it.
    /** Sizing the destination once turns a 500 MB write into one allocation rather than a series
     *  of grow-and-remap steps. libarchive knows an entry's uncompressed size from its header in
     *  the common case, so this is usually available. Optional: omit it when the size is genuinely
     *  unknown and the sink will grow. */
    [[nodiscard]] virtual std::expected< void, ModuleError > reserve( std::size_t bytes ) = 0;

    //! Appends bytes to the output. The sink owns the destination; the module keeps no copy.
    [[nodiscard]] virtual std::expected< void, ModuleError > write( std::span< const std::byte > bytes ) = 0;
};
```

`GeneratorModuleI::generate` changes shape accordingly:

```cpp
[[nodiscard]] virtual std::expected< void, idhan::ModuleError > generate(
    ModuleCallData& data,
    std::array< std::byte, 256 / 8 > desired_hash,
    ModuleSink& out ) = 0;
```

`ThumbnailerModuleI` and `MetadataModuleI` keep their return types. A thumbnail is bounded by its
target dimensions and `MetadataInfo` is control data; neither is worth an ABI change.

## 5. Backends

Modules never name a backend; they see `ModuleFile` and `ModuleSink`.

| Concrete type | Backing | Used when |
|---|---|---|
| `ipc::BlobFile` | a read-only mapping of whatever descriptor arrived | **every** module call |
| `ipc::MemfdSink` | sealed memfd, written with `pwrite` | every module result |

There is one input path and one code path in the worker. `mmap` does not care whether the
descriptor it is given is a cluster file or an anonymous memory object, so the worker maps what it
was sent and asks no questions. `INPUT_KIND` does not exist.

**What the descriptor points at is the server's business.** For a record it is the cluster file,
opened `O_RDONLY` — no copy, demand-paged. For bytes that arrived over HTTP and were never a file
(`parseMime`, `generateThumbnail`) or that a module produced in its own heap
(`ModuleFile::fromBytes` crossing a process boundary) the server writes them to a sealed memfd and
sends that. Those cases genuinely have no file to map, so the copy is inherent rather than
incidental; it is bounded by the request body, not by a media file.

**The worker closes the descriptor once the mapping exists.** A mapping outlives its descriptor, so
this costs nothing and removes the `/proc/self/fd` entry — a partial mitigation only, since
`/proc/self/maps` still names the file, but there is no reason to leave two ways to learn the same
thing. It happens in the receive path, before any module code runs.

`MemfdSink::reserve( n )` `ftruncate`s the memfd to `n`; `write()` `pwrite`s at the cursor. Writes do
not go through a mapping: the copy is identical either way, and `pwrite` avoids both remapping on
growth and having to unmap before sealing, since `F_SEAL_WRITE` is refused while a shared writable
mapping exists.

At the end of a `GENERATE` the worker seals the memfd and attaches it to the `RESULT` frame.

## 6. The shared mapping

### 6.1 The path

```
server                                   worker
------                                   ------
open( cluster_path, O_RDONLY )
fstat -> size
                    --- CALL + fd --->
                                         mmap( fd, size, PROT_READ, MAP_PRIVATE )
                                         close( fd )
                                         ModuleFile::read() -> memcpy from the mapping
```

`MAP_PRIVATE` rather than `MAP_SHARED`: nothing writes, and a private mapping cannot be used to
modify a file the server still holds open even if a module were compromised into trying.

The server keeps its descriptor for the call's duration — a nested call reusing this input
(`INPUT_REF`, §8) needs it, and it is what the file's lifetime is anchored to.

### 6.2 What this costs and what it does not

**No copy, and no memory beyond what is touched.** The mapping is demand-paged from the page cache
the file already occupies. Reading a header faults in a page; reading nothing faults in nothing. The
kernel can evict those pages under pressure and re-fault them later, which is exactly what it cannot
do with an anonymous memfd.

**A truncated file is a `SIGBUS`, not a short read.** If the file shrinks while mapped, touching a
page past the new end raises `SIGBUS` in the worker. Cluster files are content-addressed and are not
rewritten in place, so this needs an operator deleting a file mid-call — which kills that worker and
fails that call, and the pool already handles a worker dying. Worth knowing rather than worth
defending against.

**The path is visible.** `/proc/self/maps` names the mapped file. Closing the descriptor removes the
`/proc/self/fd` entry but not this one. §10 is what closes it.

### 6.3 Why not io_uring

A registered ring slot is an index rather than a descriptor, so it is the only mechanism that hides
the path *and* avoids the copy. It was designed, prototyped and measured, and then dropped: the
transport turned out to require either fork inheritance with per-worker slot tables or a seccomp
supervisor per worker, and the property it uniquely bought is one the sandbox has to provide anyway
for every other reason a worker should not be reading `/proc`.

Appendix A records the whole investigation, including two kernel behaviours that are not documented
anywhere obvious and that a future attempt would otherwise rediscover the hard way.

## 7. Protocol changes

Almost nothing. A `CALL` already carries one descriptor; it now points at the record rather than at a
copy of it.

`ipc::field` gains:

| Field | Meaning |
|---|---|
| `FILE_SIZE` | Total size of the input |
| `INPUT_REF` | On a `CALLBACK`: the `call_id` whose input should be reused (§8) |

`FILE_SIZE` is redundant for a mapping — the worker could `fstat` the descriptor before closing it —
but it is sent anyway so the size is settled by the side that opened the file. Two sources of truth
for a length that indexes into a mapping is how out-of-bounds reads get written.

There is no `INPUT_KIND`: one mechanism, no branch. `MAX_FRAME_FDS` stays at 8.

## 8. The callback path

`ArchiveThumbnailer.cpp:153` passes `file_view` — the whole archive — to `m_callbacks.generate`.
Today the runner copies the entire archive into a second memfd, ships it to the server, and the
server dispatches it straight back to the same process for `ArchiveGenerator`. One extra whole-file
copy per archive thumbnail.

All three callbacks move to handles:

```cpp
using ThumbnailFunc = std::function< std::expected< ThumbnailInfo, ModuleError >(
    const ModuleFile&, Json::Value, std::string ) >;
using GenerateFunc  = std::function< std::expected< std::unique_ptr< ModuleFile >, ModuleError >(
    const ModuleFile&, std::array< std::byte, 256 / 8 >, Json::Value, std::string ) >;
using ProbeFunc     = std::function< std::expected< ModuleCapability, ModuleError >(
    const ModuleFile&, std::string ) >;
```

When the handle a module passes *is* the input of the call it is currently serving, the runner sends
`INPUT_REF` and no descriptor. The server still holds that call's open file, keyed by `call_id`, and
sends the same descriptor again for the nested call. The copy disappears entirely.

A referenced input also carries its MIME across from the parent call, because the server cannot
re-derive one. MIME detection reads content, and on this path the server has no copy of the content
to read — not making one is the whole point. It does not need to: the referenced input belongs to a
call whose MIME was resolved before it was dispatched, so the answer is already known and is exact
rather than re-guessed.

The nested call maps the file independently, so a worker serving an archive thumbnail holds two
mappings of the same file. They share page cache, so the second costs address space and nothing
else.

`generate` returning `std::unique_ptr< ModuleFile >` is what closes the loop: the nested generator's
output memfd is handed back as a readable handle, so `ArchiveThumbnailer` can pass it directly to the
thumbnail callback without ever materialising the member in its own heap.

A handle created by `ModuleFile::fromBytes` still costs one copy into a memfd when it crosses a
process boundary. That is unavoidable — the bytes are in the module's heap, and there is no file to
map. The receiving worker maps that memfd exactly as it would map a record (§5), so nothing
downstream knows the difference.

## 9. Module conversions

All the affected modules convert together — ten files across the four premade libraries.

| Module | Today | After | Extra allocation |
|---|---|---|---|
| FFmpeg metadata + thumbnail | `OpaqueInfo` cursor over `data_view` (`ffmpeg.hpp:27`) | same cursor, one `read()` per AVIO callback | **none** — reads land in the `AVIOContext` buffer the module already allocates |
| Archives metadata + generator | `archive_read_open_memory` | `archive_read_open` with a read callback | 64 KiB fixed chunk, matching `archives.cpp:116` |
| vips metadata + thumbnail | `vips_thumbnail_buffer` / `new_from_buffer` | `VipsSourceCustom` + `vips_thumbnail_source` | **none** — reads land in the block the read signal supplies |
| PSD metadata + thumbnail | raw pointer walk | allocates one whole-file buffer and reads into it | file size |

vips 8.18.4 is installed; `vips_thumbnail_source` needs 8.9+.

Connecting a **seek** signal to the vips source alongside **read** is not optional. With only a read
signal vips treats the input as a pipe and spools the whole thing to a temporary file, which would
quietly undo the conversion while still producing correct thumbnails — the failure mode is a
performance regression that no test would catch.

The archive modules have a lifetime constraint that outlives this spec: libarchive holds a pointer
into the reader's chunk buffer, so the reader must be declared before the `archive*` handle it is
opened on and destroyed after it. Reversing the two declarations is silently wrong.

FFmpeg and archives are where the memory actually returns: a video no longer materialises to decode
one frame, and a CBZ no longer materialises to walk its member headers. `ArchiveGenerator` gains the
sink, so extraction streams from libarchive into shared memory in one pass.

PSD is the one module that genuinely cannot stream — its parser walks a contiguous pointer. Forcing
it to write that allocation in its own code is the intended outcome: the cost becomes visible at the
site that incurs it. **Adjacent and not in scope:** `PsdThumbnailer` currently holds three redundant
76 MB buffers for one thumbnail (229 MB peak). This conversion touches that code and would be a
natural moment to collapse them, but it is a separate defect and is not folded in here.

## 10. Process hardening

Between `fork()` and `execl()` in `WorkerProcess::start()`, all async-signal-safe:

| Step | Purpose |
|---|---|
| `close_range( 4, ~0U, 0 )` | fd 3 is the channel, 0–2 are stdio. Closes the inherited-descriptor hole |
| `prctl( PR_SET_NO_NEW_PRIVS, 1, ... )` | blocks setuid escalation; **required** before a seccomp filter can be installed unprivileged |
| `prctl( PR_SET_PDEATHSIG, SIGKILL )` + `getppid()` recheck | workers currently outlive a killed server |

The channel is the only descriptor a worker starts with. Inputs arrive on it per call and are closed
once mapped (§5).

### 10.1 Confinement is a dependency, not a follow-up

The follow-up sandbox spec owns the seccomp allowlist and the Landlock ruleset. What changed with
§6 is its **status**: it is no longer only defence in depth, it is what closes the path leak this
design accepts. Until it lands, a compromised worker can read `/proc/self/maps` and learn the
cluster layout.

That spec must therefore treat as acceptance criteria:

- a worker cannot read `/proc/self/maps` or `/proc/self/fdinfo/*`;
- a worker cannot open anything under the cluster root by path, so learning the layout does not
  become reading it;
- the ruleset is installed after `dlopen`, `VIPS_INIT` and module `startup()`, and before the first
  call is served — the `WorkerRunner::run()` boundary.

**One open question it needs answered first:** whether any module library `dlopen`s lazily *during* a
call rather than at startup. That determines whether the boundary above can be a hard lockdown or has
to keep `openat` open for library paths. Worth answering with `strace` on a real call rather than
discovering it as a failure.

Recorded in `docs/known-issues.md` so the gap is visible while it is open.

### 10.2 `IDHAN_HARDEN`

```cmake
if( CMAKE_BUILD_TYPE STREQUAL "Debug" )
    set( IDHAN_HARDEN_DEFAULT OFF )
else()
    set( IDHAN_HARDEN_DEFAULT ON )
endif()

option( IDHAN_HARDEN "Block same-uid ptrace of the server and disable core dumps" ${IDHAN_HARDEN_DEFAULT} )
```

Two things sit behind it, both trading debuggability for containment:

- **`prctl( PR_SET_DUMPABLE, 0 )` on the server.** Without it a compromised worker can `ptrace` the
  server — same uid — and take the database credentials and everything else, which makes the rest of
  the sandbox moot on any host with `yama.ptrace_scope=0`. Costs `gdb -p` against the server.
- **`RLIMIT_CORE = 0` on workers.** A core dump writes decoded media to disk. Costs the core dump
  from a worker that a malformed file just killed, which is exactly what you want when debugging a
  codec crash — hence the same switch rather than unconditional.

The server logs one line at startup when hardening is active. A `gdb -p` that fails with "Operation
not permitted" and no explanation is an hour lost for no reason.

`RLIMIT_AS` is deliberately **not** set. §6.4 of the worker-framework spec argues a hard ceiling
kills a worker mid-task on a legitimate large-video burst, and the soft RSS watermark already covers
the leak case. That decision stands.

## 11. Portability

No fallback path, because there is nothing to fall back from: `mmap` of a file descriptor received
over a unix socket is available everywhere IDHAN runs, needs no syscall a container runtime blocks,
and has no kernel version floor worth stating.

This is a straightforward improvement on both alternatives. The memfd path worked everywhere but
copied; the io_uring path copied nothing but would not have started under Docker's default seccomp
profile, which blocks `io_uring_setup` (`docs/docker.md:9`) and would have forced either a custom
profile or a fallback nobody exercised. The mapping needs neither.

Read errors surface as a `ModuleError` through `std::expected`. No exceptions cross the module
boundary, matching the existing convention. The one failure that does not arrive that way is the
`SIGBUS` in §6.2, which kills the worker and is handled as a worker death.

## 12. Testing

`tests/` has no coverage of the IO layer today, and the runner is a separate process. Scoped to what
can actually be asserted:

- **Read correctness.** `BlobFile` returns byte-identical content to a plain `pread` of the same
  file, across sizes spanning the page boundaries: 0, 1, 4095, 4096, 4097, 65536, and one spanning
  several pages. The oracle is the file itself.
- **Reads past the end return 0, not an error.** A demuxer probing beyond the end is normal, and both
  the ABI and every converted module depend on it.
- **Descriptor closure.** After the worker maps its input, no descriptor for it remains in
  `/proc/self/fd`, and reads still work.
- **Sink round trip.** `MemfdSink` with and without `reserve()`, asserting the sealed memfd matches
  what was written.
- **`INPUT_REF` round trip.** A callback that passes its own input back reaches the nested module
  with identical bytes and no second descriptor on the wire.
- **Module equivalence.** Each converted module produces byte-identical output to the pre-conversion
  implementation for a fixture of each handled type. This is the real regression risk in this work —
  ten files changed how they read their input, and a subtly wrong seek in a custom source produces a
  plausible-looking wrong thumbnail rather than an error.

The first four need no database and no server, so they can run standalone.

## 13. Out of scope

- **The seccomp allowlist and Landlock ruleset.** The follow-up sandbox spec — but see §10.1: it is
  now a dependency of this one, not an independent improvement.
- **Restricting what the server may map.** Nothing here stops a bug in the server opening the wrong
  file; the worker is confined to what it is given, not to what it should have been given.
- The `PsdThumbnailer` triple-buffer defect (§9).
- Any change to `MetadataInfo`, `ThumbnailInfo`, or the thumbnail return path.

## Appendix A: the io_uring investigation

This design originally used a restricted io_uring instead of a mapping, because a registered ring
slot is an index rather than a descriptor — nothing to `readlink`, nothing in `/proc/self/maps` — so
it hides the path *and* avoids the copy, which no mapping can do. It was prototyped to working code
before being dropped.

**Why it was dropped.** Not because it does not work — it does, and the probes prove each piece. It
was dropped because the transport was expensive in machinery, and the property it uniquely bought is
one the sandbox must provide anyway. A worker that can read `/proc/self/maps` can read plenty of
other things it should not; solving that once, in the sandbox, is better than solving it a second
time in the input path.

**What the probes establish**, in
[`2026-08-02-module-sandbox-io-uring-probes/`](2026-08-02-module-sandbox-io-uring-probes/) with
recorded output from kernel 7.1.5-arch1-2, liburing 2.15:

| Claim | Result |
|---|---|
| fdinfo leaks the registered path | **Confirmed** — printed `0: /etc/hostname` |
| `READ` + `IOSQE_FIXED_FILE` | succeeds |
| `READ` without `IOSQE_FIXED_FILE`, `OPENAT` | `EACCES` |
| `FILES_UPDATE`, no `REGISTER_OP` restriction | **succeeded — slot repointed** |
| `FILES_UPDATE`, with `REGISTER_OP` restriction excluding it | `EACCES` |
| fdinfo after register-and-close | unreadable; reads still work |
| **ring fd over `SCM_RIGHTS`** | **`EINVAL`** — plain and restricted alike |
| plain file fd, memfd over `SCM_RIGHTS` | sent |
| ring survives `fork` + `exec`, child maps it | yes |
| parent `FILES_UPDATE` visible to child's next read | yes |
| child `io_uring_setup` / `FILES_UPDATE` after seccomp lockdown | `EPERM` |
| inherited ring reads after lockdown | still works |
| ring fd via `SECCOMP_IOCTL_NOTIF_ADDFD` | permitted |
| memfd registered in a slot and read | works |
| sparse table; read of an unfilled slot | works; `EBADF` |

**Three findings worth keeping**, because each cost real time and none is documented anywhere
obvious:

1. **An io_uring descriptor cannot cross a unix socket.** `SCM_RIGHTS` returns `EINVAL` — a ring can
   hold registered files including unix sockets, forming cycles the unix GC cannot break. There is
   no flag. Any future attempt to hand a ring to an existing process must use fork inheritance or
   `SECCOMP_IOCTL_NOTIF_ADDFD`, both of which were shown to work.

2. **`ctx->restricted` is set by `ENABLE_RINGS`, not by `REGISTER_RESTRICTIONS`.** Registration is
   free while the ring is disabled and everything closes at enable. Getting this backwards produces
   a ring that silently accepts registrations it should refuse.

3. **Ring restrictions are per-context; seccomp is per-process.** That asymmetry is what would have
   let `FILES_UPDATE` stay permitted for the server while being denied to the worker — a shared
   ring with an asymmetric control channel. It is the non-obvious idea in the whole investigation
   and is worth remembering independently of io_uring.

**If it is ever revisited,** the shape that worked was: server builds a restricted ring with a sparse
N-slot file table before `fork`, the worker inherits it across `exec`, maps it, register-and-closes
the ring fd to kill the fdinfo leak, then installs a seccomp filter denying `io_uring_setup` and
`io_uring_register`; the server repoints a slot per call with `FILES_UPDATE`. `inherit_probe.c` is
that design in about 200 lines.
