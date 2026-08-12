# Known issues

Defects and deferred work that are understood but not yet fixed. Each entry records what is wrong,
why it was deferred, and what fixing it would involve — so the next person to touch the area does not
have to rediscover it.

Entries are removed when fixed, not marked done.

---

## PSD thumbnailing holds three redundant copies of the image

**Where:** `IDHANModules/premade/psd/PsdThumbnailer.cpp`

One PSD thumbnail peaks at roughly 229 MB for a 76 MB image, because the decoded image is held in
three buffers at once. Two of the three are avoidable — the intermediate copies exist because each
stage allocates its own destination rather than writing into the next stage's buffer.

**Why deferred:** it is a self-contained defect in one module, unrelated to the ABI work that
happens to touch the same file. Folding it into that change would make a regression harder to
attribute — a thumbnail that comes out wrong afterwards could be either the streaming conversion or
the buffer collapse.

**Fixing it:** collapse the intermediate allocations so each stage writes into the buffer the next
stage consumes. Worth doing immediately after the `ModuleFile` conversion lands, while the code is
fresh, rather than much later.

---

## Module workers have no syscall or filesystem confinement

**Where:** `IDHANServer/src/modules/WorkerProcess.cpp`, `IDHANModules/runner-src/`

A worker that a crafted file has compromised can still read the entire filesystem as the server's
uid: the config file, every cluster, the database credentials. The process hardening in the
io_uring spec (`close_range`, `NO_NEW_PRIVS`, `PDEATHSIG`, `PR_SET_DUMPABLE`) closes descriptor
inheritance and ptrace, but nothing restricts what the worker may open.

**Why deferred:** a seccomp allowlist and Landlock ruleset have to be derived from what libvips,
FFmpeg and libarchive actually do at runtime, which needs empirical characterisation. Getting it
wrong does not fail loudly — it fails as a specific image format mysteriously not thumbnailing.
That is its own piece of work, not a rider on an unrelated change.

**Fixing it:** a follow-up spec. One acceptance criterion is already known: a worker must not be
able to open `/proc/self/fdinfo/*`. Closing that leak at its source is worth doing regardless, but
the guarantee should not rest on a single call site staying correct.

---

## `ModuleFile` and `ModuleSink` have no Windows backends

**Where:** `IDHANModules/ipc/src/`

The interfaces are platform-neutral but only the Linux backends exist, consistent with modules being
Linux-only today. A Windows port would add `ipc/src/windows/` implementations without changing any
caller or any module.

**Why deferred:** nothing runs modules on Windows yet, so there is no way to verify a backend that
was written.

---

## `IDHAN_HARDEN` trades away two debugging affordances

**Where:** `CMakeLists.txt` (the `IDHAN_HARDEN` option), `IDHANServer/src/modules/ModuleLoader.cpp`
(`applyHardening`, the process-wide `PR_SET_DUMPABLE(0)`), `IDHANServer/src/modules/WorkerProcess.cpp`
(the per-fork `RLIMIT_CORE` clamp)

With `IDHAN_HARDEN` on — the default outside Debug builds — `gdb -p` cannot attach to the server
(`PR_SET_DUMPABLE(0)`) and a worker killed by a malformed file leaves no core dump
(`RLIMIT_CORE = 0`).

This is a deliberate trade, not a defect: without `PR_SET_DUMPABLE(0)` a compromised worker can
`ptrace` the server on any host with `yama.ptrace_scope=0`, which would make the rest of the sandbox
moot. It is recorded here because the cost lands on whoever is debugging a codec crash, who may not
know the switch exists.

**If it becomes painful:** build with `-DIDHAN_HARDEN=OFF`, or use a Debug build where it is off by
default. Revisit only if the workaround proves insufficient in practice.

## Module workers can read the cluster path from `/proc/self/maps`

A worker receives its input as a read-only mapping of the record file itself, which is what removes
the whole-file copy the previous memfd path incurred. The cost is that `/proc/self/maps` names the
mapped file, so code executing inside a worker -- the threat model's crafted-file-exploits-a-codec
case -- can recover the cluster path, the sharding layout, and the fact that filenames are hashes.

The descriptor is closed as soon as the mapping exists, so `/proc/self/fd` says nothing, but that is
a partial mitigation only.

**Why it is open:** hiding the path without copying requires a handle that is not a nameable object.
Only an io_uring registered slot qualifies, and that approach was designed, prototyped and rejected
for its cost.

**What closes it:** the follow-up sandbox spec, denying `/proc` via Landlock or a mount namespace.
That work was already planned; this raises it from defence in depth to the thing this leak depends
on. It should also deny opening anything under the cluster root by path, so that learning the layout
does not become reading it.
