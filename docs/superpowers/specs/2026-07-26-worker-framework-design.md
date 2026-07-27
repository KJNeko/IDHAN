# Worker Framework Design

**Date:** 2026-07-26
**Status:** Approved (framework layer only; module consumer is a follow-up spec)

## 1. Motivation

Module operations (thumbnailing, metadata parsing, file generation) run untrusted-in-practice
native code (libvips, FFmpeg, libarchive, PSD). A malformed input can segfault, abort, hang in an
infinite loop, or allocate without bound. Today modules are `dlopen`'d directly into the server
process, so any such failure takes the entire server down with it.

This spec defines a **general, reusable worker framework** for offloading work to supervised,
separate-binary child processes with crash, hang, and memory isolation. The module system will be
its first consumer, designed in a follow-up spec. Nothing in this framework is module-specific:
payloads are opaque bytes.

A second goal shapes the transport: the IPC is built on io_uring, which operates on *any* file
descriptor. The same async read/write path drives a local `socketpair` today and can drive a TCP
socket to another machine later, making remote worker machines a future extension rather than a
rewrite.

## 2. Architecture

Three tiers, each a separate process:

```
IDHANServer ──channel──▶ IDHANMonitor ──channels──▶ IDHANWorker × N
  (one multiplexed          (owns + supervises          (each runs one
   connection, many          the worker pool)            job at a time)
   in-flight requests)
```

- **The server only ever talks to the Monitor.** The Monitor is a **broker in the request path**,
  not an out-of-band babysitter. The server has exactly one endpoint to reason about.
- The **Monitor** owns the worker pool: it spawns workers, dispatches each request to an idle
  worker, enforces timeouts and memory limits, kills and respawns on failure, and relays responses
  back to the server.
- Each **Worker** executes **one job at a time**. Pool size provides concurrency. A timeout or crash
  therefore has a blast radius of exactly one job.

### 2.1 Why a broker, not direct server→worker

- The server has a single, stable connection regardless of pool size or worker churn (respawns are
  invisible to the server).
- It localizes all supervision policy (dispatch, timeout, respawn, limits) in one place.
- It yields a clean remote story: a Monitor is a *per-machine* worker manager. A future remote
  deployment puts the Monitor on the remote host — server↔Monitor over TCP (inline blobs),
  Monitor↔Worker local on that host (memfd blobs) — with no change to workers or the server's view.

## 3. Component layout

### 3.1 `IDHAN` (shared library) — the transport home

The io_uring backend currently in `IDHANServer/src/filesystem/io/` moves into `IDHAN` so every tier
can link one copy of the wire format. `IDHAN` gains:

- **io_uring backend** (moved): `IOUring`, `FileIOUring`, the Linux implementation and awaiters, the
  Windows implementations. Extended with **`sendmsg`/`recvmsg`** support (the current abstraction has
  only positional read/write); this is required for `SCM_RIGHTS` fd passing.
- **`Channel`** — framed, multiplexed RPC over any fd (see §4).
- **`Blob`** — large-payload delivery with pluggable strategy (see §5).
- **Protocol/message types** — the frame header and message-type enum (see §4).
- **Generic RPC client/server helpers** — the request/response plumbing the server, Monitor, and
  Worker all build on.

#### Decoupling io_uring from drogon/trantor

The io_uring core must be usable in the Monitor and Worker, which have no drogon/trantor event loop.
Today the layer is coupled to trantor in two places, both of which are removed as part of the move:

1. **Return type.** `IOUring::read`/`write` return `drogon::Task<T>`. Replace with a **neutral,
   self-contained coroutine task type** defined in `IDHAN` (a minimal `Task<T>` with the standard
   promise/final-suspend continuation handoff). No drogon dependency.
2. **Resumption affinity.** `ReadAwaiter/WriteAwaiter::await_suspend` captures
   `trantor::EventLoop::getEventLoopOfCurrentThread()` and `complete()` calls
   `m_event_loop->queueInLoop(m_cont)`. In a process with no trantor loop this returns null and
   crashes. Replace the hard trantor reference with a pluggable **`Resumer`** interface
   (`virtual void resume(std::coroutine_handle<>) = 0`, or an equivalent type-erased callback)
   captured at suspend. The io_uring watcher thread hands the completed continuation to the
   `Resumer` instead of to a trantor loop directly.

    - **Server:** supplies a `TrantorResumer` adapter wrapping
      `getEventLoopOfCurrentThread()->queueInLoop`, preserving today's loop-affinity behavior exactly.
    - **Monitor / Worker:** supply their own `Resumer` bound to their process's run loop (each pumps
      io_uring completions and drives its own coroutines; see §3.2/§3.3).

The goal is a `IDHAN` io_uring/transport core with **no drogon or trantor dependency**. Any
consumer that still wants `drogon::Task` interop gets it via a thin adapter at its own boundary
(e.g. the server's monitor client bridges the neutral `Task` back onto a drogon coroutine), not by
pulling drogon into `IDHAN`.

The move is scoped to the transport-relevant pieces only; unrelated filesystem code (e.g. cluster
path handling) stays in `IDHANServer`. Includes across `IDHANServer` that reference
`filesystem/io/...` are repointed to the new `IDHAN` location as a mechanical follow-through.

### 3.2 `IDHANMonitor` (new executable) — the supervisor/broker

- Launched and watched by the server. Owns the worker pool.
- Accepts **one multiplexed connection** from the server and fans requests out to idle workers, one
  job per worker.
- Enforces per-request timeouts, `RLIMIT_AS` on workers, crash detection, and respawn (see §6).

### 3.3 `IDHANWorker` (new executable) — the child runtime

- Parses argv (channel fd, config, index), initializes its own `IOUring` instance, applies its
  memory limit, then runs the serve loop: read a `Request`, invoke the registered `Handler`, write
  the `Response`/`Error`. Executes one request at a time.
- The `Handler` is an injected callback `Response(Request)`. This framework provides the loop,
  framing, blob mapping, and lifecycle; the **consumer** provides the handler and links its own code.
  In this spec the worker ships with a trivial/test handler only; the module handler is the
  follow-up spec.

### 3.4 `IDHANServer` — the monitor client

- Gains a thin **monitor client** (from `IDHAN`): it launches the Monitor as a child, connects the
  channel, and exposes `IDHANTask<std::expected<Response, WorkerError>> dispatch(Request)`. The
  calling Drogon coroutine suspends until the reply arrives and never blocks the event-loop thread.

## 4. Transport — `Channel` and protocol

- A `Channel` wraps one fd and performs async framed message I/O via the `IOUring` backend. The fd
  is one end of a `socketpair(AF_UNIX, SOCK_SEQPACKET)` locally; a TCP socket for a future remote
  link. Same io_uring code path either way.
- **Frame header** (fixed size): `{ magic, request_id (u64), type, flags, inline_len }`, followed by
  `inline_len` bytes of opaque control payload. Control payloads are small: MIME strings, dimensions,
  JSON, error text. Large bytes never travel inline — they travel as a `Blob` (§5).
- **`request_id`** correlates a response to its request. The protocol permits **out-of-order
  replies**, which is why the server↔Monitor link is genuinely multiplexed (many requests in flight
  on one connection) even though each worker runs one job at a time.
- **Message types:** `Request`, `Response`, `Error`, `Cancel`, `Ping`/`Pong` (liveness).

### 4.1 Multiplexing across the tiers

- **Server ↔ Monitor:** one connection, many concurrent requests, correlated by `request_id`,
  out-of-order responses expected.
- **Monitor ↔ Worker:** one channel per worker, **one active request at a time**. The Monitor holds
  a request→worker routing map so it can attribute a worker's crash/timeout to exactly one
  outstanding server request.

## 5. Payload delivery — `Blob`

Large bytes (input file contents, result bytes) are delivered as a `Blob`, whose strategy is chosen
by the channel's capabilities:

- **Local — shared memory (implemented now):** `memfd_create` + `mmap`; the fd is handed to the peer
  via `SCM_RIGHTS`. Zero-copy in both directions. The **Monitor relays the fd** (via `SCM_RIGHTS`)
  from server to worker and the result fd back, never copying payload bytes — this is what makes the
  extra broker hop cheap.
- **Remote — inline stream (interface reserved, not implemented):** bytes streamed over the channel
  via io_uring. The `Blob` interface and capability negotiation are designed so this drops in for a
  future remote transport without touching consumers.

## 6. Supervision and failure handling

### 6.1 Ownership chain — no orphans

`server → monitor → workers`. Each child sets `PR_SET_PDEATHSIG(SIGKILL)` tied to its parent, and
each parent reaps and respawns its children.

- **Server dies:** the Monitor receives `PDEATHSIG`, cleanly kills its workers, and exits. Nothing is
  leaked to `init`.
- **Monitor dies:** its workers die with it (`PDEATHSIG`); the server detects the closed channel and
  relaunches the Monitor (which respawns a fresh pool).
- **Worker dies:** the Monitor reaps it, fails the one in-flight request with a `WorkerError`, and
  respawns a replacement.

### 6.2 Dispatch policy

The Monitor tracks idle/busy workers and routes each request to an idle worker, marking it busy until
its response arrives. Concurrency equals pool size. Pool size defaults to mirror the existing job
system: 25% of hardware threads, minimum 2.

### 6.3 Timeout

Each dispatched request carries a deadline. On expiry the Monitor `SIGKILL`s the worker (only its one
job dies), returns a timeout `WorkerError` to the server for that `request_id`, and respawns the
worker. Because the blast radius is already one job, no cooperative-cancel handshake is required.

### 6.4 Memory watermark (soft, checked between tasks)

Memory is **not** capped with a hard `RLIMIT_AS`: a strict ceiling would kill a worker mid-task on a
legitimate bursty allocation (large-video decode, big archive), which is exactly the workload we want
to allow. Instead the limit is a **soft watermark checked only at task boundaries**, when the
worker's memory should have returned to baseline:

- After a worker completes a task and its response has been delivered, the Monitor reads the
  worker's resident memory (`/proc/<pid>/statm` — it knows the pid) and compares it against
  `workers.mem_watermark`.
- If the worker is over the watermark **while idle**, that indicates memory that should have been
  released was not (a leak accumulating across tasks). The Monitor **gracefully retires** the
  worker — it is already idle, so no in-flight job is lost — and respawns a fresh replacement.
- Because the check happens between tasks, a within-task burst that peaks above the watermark but
  frees back down before returning is never penalized.

The per-request **timeout (§6.3) is the backstop** for a pathological *within-task* runaway that
never returns: such a task is `SIGKILL`ed at the deadline regardless of memory. (An absolute
host-protection hard ceiling may optionally be set far above the watermark, but it is off by default;
the soft watermark plus timeout are the intended mechanism.)

### 6.5 Error surface

All framework failures reach the server as a typed `WorkerError` (variant: `Crash`, `Timeout`,
`Protocol`, `MonitorUnavailable`). Consumers map these onto their own domain errors (e.g. the module
layer maps them to `ModuleError`). Note a memory-watermark **retire (§6.4) is not a request error**:
the task that triggered it already returned its result successfully; the retire is a silent
lifecycle event invisible to the caller.

## 7. Configuration

New config keys (namespaced for the framework; the module consumer may add its own on top):

| Key                     | Default               | Purpose                                                                 |
|-------------------------|-----------------------|-------------------------------------------------------------------------|
| `workers.count`         | 25% hw threads, min 2 | Worker pool size                                                        |
| `workers.timeout`       | 2 min                 | Per-request deadline before kill (generous for large-video work)        |
| `workers.mem_watermark` | e.g. 2 GiB            | Soft between-tasks RSS threshold; over it while idle → retire + respawn |

Binary discovery: the Monitor and Worker binary paths are derived from the server binary's own
directory (they build alongside it in `build/bin/`).

## 8. Testing

Framework-level, no modules involved:

- A **test worker handler** that can, on command: echo a payload, sleep past the timeout, `abort()`,
  and two memory behaviors — (a) a within-task burst that peaks above the watermark then frees back
  down before returning, and (b) a leak that stays above the watermark after returning. Tests assert
  the server receives the correct `WorkerError` variant for timeout/crash and that the Monitor
  respawns afterward; that behavior (a) delivers its result and the worker is **not** retired; and
  that behavior (b) delivers its result and *then* the worker is retired and respawned.
- A **blob round-trip** test asserting a memfd payload of non-trivial size arrives byte-identical at
  the worker and the result blob arrives byte-identical back at the server, across the broker hop.
- A **multiplexing** test issuing many concurrent requests over the single server↔Monitor connection
  and asserting each response is correctly correlated to its request.
- An **ownership** test asserting that killing the Monitor kills its workers, and that killing the
  server kills the whole tree (no orphaned processes).

## 9. Out of scope (this spec)

- The module handler and module-specific request/response encoding — follow-up "module worker"
  spec, which registers a handler in `IDHANWorker` and swaps the server's module call sites onto the
  monitor client.
- The remote/TCP transport and inline-stream `Blob` strategy — designed-for, not implemented.

## 10. Open implementation notes (resolve during planning)

- Exact extent of the io_uring move into `IDHAN` vs. what stays in `IDHANServer` (the neutral
  `Task<T>` and `Resumer` boundary is decided in §3.1; what remains is drawing the file-by-file line).
- The Monitor's and Worker's internal run loop: whether io_uring completions resume coroutines
  inline on the watcher thread or are posted to a dedicated loop thread via their `Resumer`.
- Whether the monitor client lives in `IDHAN` or `IDHANServer` (leaning `IDHAN`, since it is generic
  channel/RPC plumbing).
- Frame header exact field widths and endianness (fix a canonical little-endian on-wire layout for
  the future remote case).
