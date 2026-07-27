# Tracy Profiling Integration — Design

**Date:** 2026-07-21
**Status:** Draft — pending user review
**Scope:** IDHANServer only (client/importer out of scope)

## Goal

Integrate the [Tracy](https://github.com/wolfpld/tracy) frame profiler into IDHANServer to get
**full coroutine timelines** — a continuous per-coroutine trace that survives the thread hops
inherent to Drogon's async model (a handler starts on an io thread, `co_await`s a DB query, and
resumes on the DB connection's loop thread). Synchronous CPU hot paths get plain zone
instrumentation on top.

The integration must be **zero-footprint when disabled**: a build without `IDHAN_ENABLE_TRACY`
compiles every Tracy macro to a no-op and does not alter the coroutine code path at all.

## Background / constraints

- **Build**: static-lib C++23 monorepo. Drogon is a vendored git submodule pinned to a pristine
  upstream tag (`v1.9.13`); its `Task::promise_type` has no `await_transform`. FGL CMake helpers
  (`AddFGLExecutable`) build `IDHANServer`.
- **Threading**: N drogon io-loop threads (`app.setThreadNum`), a DB connection pool (≤16), and the
  `JobRuntime` on its own trantor loop pool. Coroutines migrate across these threads at every
  suspension point.
- **Coroutine types**:
  - `drogon::Task` — ~354 uses, including **all top-level API handlers** (`ResponseTask =
    drogon::Task<HttpResponsePtr>`). Vendored, not owned by IDHAN.
  - `IDHANTask<T>` / `ExpectedTask<T>` (`= IDHANTask<std::expected<...>>`) — ~89 uses, IDHAN-owned.
  - `JobTask` (`JobTaskPromise`) — job coroutines, IDHAN-owned.
  - All three share the same awaiter shape: lazy `initial_suspend`, `drogon::task_awaiter` /
    `drogon::final_awaiter`, `setContinuation` / `continuation_`.

## Why fibers (the core technical fact)

Tracy's `ZoneScoped` is an RAII object tracked on a **thread-local zone stack**. If a zone spans a
`co_await`, its destructor runs on whatever thread resumed the coroutine — a different thread —
corrupting the stack. Tracy's mechanism for exactly this is **fibers** (`TRACY_FIBERS`): a fiber
owns its *own* zone stack, which Tracy **saves on `TracyFiberLeave` and restores on
`TracyFiberEnter`**. The rule becomes: bracket every suspension point (leave before suspend, enter
after resume) and then zones may freely span awaits.

Each coroutine **instance** is modeled as one Tracy fiber.

## Architecture

### Component 1 — `profiling/tracy.hpp` (the wrapper header)

A single header that is the only place the rest of the server includes Tracy through.

- When `IDHAN_ENABLE_TRACY` is defined: `#include <tracy/Tracy.hpp>` and re-export the macros.
- When not defined: define `ZoneScoped`, `ZoneScopedN(x)`, `FrameMark`, `TracyFiberEnter(x)`,
  `TracyFiberLeave`, etc. as empty no-ops.

This keeps `#ifdef` out of call sites — code always writes `ZoneScoped;`.

### Component 2 — Fiber instrumentation on coroutine promises

Applied identically to **three** promise types: `IDHANTask::promise_type` (both `T` and `void`
specializations), `JobTaskPromise`, and — via a tracked patch — `drogon::Task::promise_type`.

Each promise gains, **guarded by `#ifdef TRACY_ENABLE` so the disabled build is byte-for-byte the
current code path**:

1. **A stable unique fiber name.** An `std::atomic<uint64_t>` counter assigns each instance an id;
   the name is stored in the promise (owning `std::string`, so `.c_str()` is stable for the frame's
   lifetime = the fiber's lifetime). Optional readable prefix from a thread-local "current request
   tag" (see Component 4), e.g. `"GET /search #4213"`.

2. **`await_transform`** — the centralized suspension bracket. Defined only under `TRACY_ENABLE`:
   ```cpp
   template < typename Awaitable >
   auto await_transform( Awaitable&& aw ) {
       return FiberAwaiter{ drogon::internal::getAwaiter( std::forward<Awaitable>(aw) ), name_ };
   }
   ```
   `FiberAwaiter` wraps the inner awaiter and forwards `await_ready` / `await_suspend` (preserving
   the symmetric-transfer return type) / `await_resume`, with:
   - `await_suspend(...)`: `TracyFiberLeave;` then delegate to the inner awaiter.
   - `await_resume()`: `TracyFiberEnter(name_);` then delegate.

   Because it routes **every** `co_await` in the body — including foreign awaitables such as
   `db->execSqlCoro(...)`, `when_all`, `sleepCoro` — no call site changes.

3. **Custom initial awaiter** whose `await_resume()` calls `TracyFiberEnter(name_)` — fires the
   instant the lazy body first runs.

4. **Custom final awaiter** wrapping `drogon::final_awaiter` — calls `TracyFiberLeave` before
   returning `continuation_`.

**Resulting behavior:** when handler `R` awaits helper `H`, Tracy shows `R` go idle, `H` run on its
own fiber track (correctly stitched across a DB→io thread hop mid-flight), then `R` resume. Full
coroutine timelines.

### Component 3 — Synchronous zone instrumentation

Plain `ZoneScoped` / `ZoneScopedN("name")` in CPU-bound synchronous leaves (no fiber machinery
needed). Starter set:

- `crypto/SHA256` hashing
- `SearchBuilder::construct()` / `query()`
- Module calls: thumbnailer, metadata, generator
- Mime detection / parser dispatch
- `db/drogonArrayBind` array binding

`FrameMark` at the job-runtime loop boundary (optional; gives Tracy a frame cadence).

This set is a starting point, not exhaustive; more zones are added as hotspots surface.

### Component 4 — Request/job correlation (optional, recommended)

A `thread_local` "current request tag" set in the existing pre-routing advice
(`ServerContext::setupCORSSupport`'s `registerPreRoutingAdvice`) from `method + path`, read by the
handler promise when it builds its fiber name. Makes fiber tracks human-readable
(`"GET /search #4213"`) instead of anonymous ids. Cleared in post-handling advice.

### Component 5 — Build system

- Vendor `tracy` as a git submodule under `dependencies/tracy`.
- `option(IDHAN_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)`.
- When ON, in `IDHANServer/CMakeLists.txt`: compile `TracyClient.cpp` into `IDHANServer`, link the
  Tracy client target, and `target_compile_definitions(... PUBLIC IDHAN_ENABLE_TRACY TRACY_ENABLE
  TRACY_FIBERS TRACY_ON_DEMAND)`.
  - `TRACY_ON_DEMAND`: the server runs normally with no profiler attached and only begins collecting
    when the Tracy GUI connects — required for a long-running server (otherwise it buffers from
    startup).
- The definitions must be `PUBLIC` / propagate to the drogon target's translation units that
  instantiate `Task::promise_type`, so the patched hooks actually compile in. (Verify propagation;
  drogon is header-heavy, so the promise is instantiated in IDHAN TUs — `PUBLIC` on IDHANServer plus
  the wrapper header include should suffice. Confirm during implementation.)

### Component 6 — The drogon patch (coverage decision)

To cover the ~354 `drogon::Task` coroutines (all handlers), `drogon::Task::promise_type` gets the
same four hooks from Component 2. Delivery: a **tracked patch file**
`dependencies/patches/drogon-tracy-fibers.patch` plus a documented apply step (README/docs note, or
a CMake `execute_process` guard that applies it when `IDHAN_ENABLE_TRACY` is ON and the patch is not
yet applied). This keeps the change **reviewable and reproducible across drogon bumps** rather than
a silently-dirty submodule. Re-applying/verifying the patch is an explicit step in the drogon-bump
workflow.

The hooks are `#ifdef TRACY_ENABLE`-guarded inside the patch, so an unpatched-but-disabled build and
a patched-but-disabled build are identical.

## Sharp edges / risks (must be handled, not assumed away)

1. **Fiber name lifetime.** Tracy retains the name pointer. The promise-owned `std::string` is valid
   for the fiber's life, but we must **verify Tracy does not dereference the name after the final
   `TracyFiberLeave` + frame destruction**. If it does, intern names into a bounded/leaky pool keyed
   by id. Treated as a verification step, not an assumption.
2. **Fiber-per-instance scaling.** This produces *thousands* of fibers over time; Tracy is happiest
   with a bounded fiber set. **Intended usage is scenario profiling** — attach the GUI, run one
   search / one import / one benchmark, capture, analyze — **not** 24/7 production tracing. Stated as
   a usage constraint, not a defect.
3. **`await_transform` completeness.** Once defined, it intercepts *every* `co_await` in the body.
   The generic must correctly normalize via `drogon::internal::getAwaiter` and forward all three
   awaiter methods including the symmetric-transfer `await_suspend` return type, for every awaitable
   the codebase uses (`execSqlCoro`, `when_all`, `sleepCoro`, nested tasks). Covered by a
   compile-and-run pass over existing endpoints/tests.
4. **Overhead when enabled.** A `FiberLeave`/`FiberEnter` pair per suspension. Acceptable for
   scenario profiling; not for always-on. `TRACY_ON_DEMAND` keeps the disabled-collection cost low.

## Non-goals (YAGNI)

- No runtime config toggle — Tracy is a compile-time build (`IDHAN_ENABLE_TRACY`); the GUI connects
  on demand.
- No instrumentation of IDHANClient or HydrusImporter.
- No fiber-per-*request* name propagation from parent to child coroutine (fiber-per-instance is
  simpler and correct; revisit only if track count becomes unworkable).
- No exhaustive zone coverage in v1 — starter set only.

## Testing / verification strategy

- **Disabled build unchanged**: build `IDHANServer` without `IDHAN_ENABLE_TRACY` and confirm no
  behavior/codegen change (existing tests pass; the coroutine path is untouched).
- **Enabled build compiles & runs**: build with `IDHAN_ENABLE_TRACY`, run the server, connect the
  Tracy GUI, and exercise a representative flow (a search, an import job).
- **Timeline correctness**: verify in the Tracy GUI that a single request appears as one fiber
  spanning its DB awaits across thread hops, and that synchronous zones (SHA256, SearchBuilder)
  appear nested correctly.
- **Name-lifetime check**: run a high-request-count scenario and confirm no use-after-free / garbage
  fiber names (sharp edge #1).

## Open items to resolve during implementation

- Confirm compile-definition propagation into the drogon `Task::promise_type` instantiations
  (Component 5).
- Choose patch-apply delivery: manual+documented vs CMake-driven (Component 6).
- Validate Tracy fiber-name retention semantics (sharp edge #1).
