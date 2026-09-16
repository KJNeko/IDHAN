# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is IDHAN

IDHAN is a C++23 media management and archival server with a booru-compatible tagging model. It is designed for large media collections and has Hydrus Network compatibility baked in (shared constants, Hydrus API endpoint compatibility, HydrusImporter tool).

## Git workflow

`dev` is the integration branch — feature/fix branches merge into `dev`, not `master`. `master` tracks releases.

## Build

**Prerequisites**: Submodules must be initialized first.

```bash
git submodule update --init --recursive
cmake -DCMAKE_BUILD_TYPE=System -B build   # use Debug or Release as alternatives
cmake --build build -j$(nproc) --target IDHANServer
# Optionally also build the Hydrus migration tool:
cmake --build build -j$(nproc) --target IDHANServer HydrusImporter
```

Output lands in `build/bin/`. The config example is auto-copied there as `config.toml`.

Pre-configured Ninja build directories usually already exist under `build/` (`debug`, `debug-strict`, `release`, `minimal-server`). Prefer reusing one to verify changes instead of configuring a fresh tree:

```bash
cmake --build build/debug --target IDHANServer
```

**Key CMake options:**

| Option | Default | Purpose |
|---|---|---|
| `BUILD_IDHAN_SERVER` | ON | Main server |
| `BUILD_IDHAN_CLIENT` | ON | Qt client shared library |
| `BUILD_HYDRUS_IMPORTER` | ON | Qt GUI import tool |
| `BUILD_IDHAN_TESTS` | OFF | GoogleTest suite |
| `BUILD_IDHAN_WEB` | ON | React WebUI in `IDHANWeb/` (requires `pnpm`; warns and skips if absent) |
| `IDHAN_DISABLE_API_AUTH` | OFF | Disable API key checks (dev only) |

## Tests

Tests require a running PostgreSQL instance with `dbname=idhan-db user=idhan password=idhan host=localhost`. `--testmode` forces the server to use the `test` schema instead of `public`.

```bash
cmake -DBUILD_IDHAN_TESTS=ON -B build
cmake --build build -j$(nproc) --target IDHANTests
ctest --test-dir build
# Or run a single test binary directly:
./build/bin/IDHANTests --gtest_filter="SuiteName.TestName" --testmode --use_stdout
```

Test fixtures live in `tests/src/db/fixtures/`. Server integration tests use `SERVER_HANDLE` macro (from `helpers/serverStarterHelper.hpp`) to spin up a real server process. DB-level tests inherit from `ServerTagFixture` or `ServerDBFixture`, which set up and tear down the schema automatically.

## Architecture

### Component map

```
IDHAN/           — shared library: types, utilities, Hydrus constants (generated from Python)
IDHANServer/     — Drogon HTTP server; all API handlers, DB logic, job system
IDHANClient/     — Qt6 shared library: typed C++ wrappers over the REST API
IDHANModules/    — plugin interface + premade modules (libvips, FFmpeg, libarchive)
IDHANMigration/  — PostgreSQL schema via numbered SQL files; run by the server on startup
tools/HydrusImporter/ — Qt6 GUI for one-time import from a Hydrus database (SQLite)
```

`dependencies/libFGL` provides the CMake helpers used throughout: `AddFGLLibrary()`, `AddFGLExecutable()`, `AddFGLModule()`.

### Key types (`IDHAN/include/IDHANTypes.hpp`)

All integer IDs are distinct type aliases (`RecordID`, `TagID`, `TagDomainID`, `ClusterID`, etc.) over `int32_t`/`int16_t`/`int64_t`. Always use these aliases rather than raw integers. `SHA256` is a custom type from `crypto/SHA256.hpp` used for all record hashes.

### Tag model

Tags are stored as `namespace:subtag` pairs. The `IDHAN/` library provides `splitTag()`. The server keeps several layers:
- **Raw mappings** — what was directly applied to a record.
- **Active mappings** — after alias, sibling, and parent resolution, surfaced via DB views (`active_tag_mappings_final`).

Tag relationships (aliases, siblings, parents) live in their own tables and are resolved by DB triggers/views rather than application code.

### Database

PostgreSQL is accessed directly over libpq (no ORM) through `idhan::db::DBContext` (
`IDHANDb/include/idhan/db/DBContext.hpp`). `ServerContext` owns the single `DBContext` for the process and passes it by
reference into route-registration functions. Query results are decoded into typed tuples via codecs, not into a generic
row/field type.

```cpp
db::DBBindings bindings {};
bindings.bind( record_id );
const auto rows { co_await db.query( "SELECT sha256, size FROM records WHERE record_id = $1", std::move( bindings ) )
                       .rows< std::span< const std::byte >, std::int64_t >() };
```

A query with no rows to decode ends in `.result()` instead of `.rows<...>()`. `DBBindings::bind()` also accepts
`std::vector<T>` directly for array parameters (e.g. `UNNEST($1::integer[])`).

**Schema migrations** live in `IDHANMigration/src/` as `N-tablename.sql`. Files are executed in ascending numeric order at server startup. When adding a migration, name it `(N+1)-tablename.sql`.

### API handler pattern

Handlers are free functions returning `http::HTTPTask<http::Response>` (an alias for `runtime::Task<http::Response>`,
itself `folly::coro::Task`). Each route family exposes one or more `fooRoute(db::DBContext&)` functions returning an
`http::RouteDefinition`; `ServerContext::startReplacementStack()` collects these into a `http::RouteRegistry` and hands
it to `http::HTTPServer`.

```cpp
static http::HTTPTask< http::Response > doThing( db::DBContext& db, http::RequestContext context )
{
    const auto id_e { context.parameters().require< RecordID >( "id" ) };
    if ( !id_e ) co_return http::Response::error( 400, id_e.error().message );

    // ... co_await db.query(...) ...

    co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

http::RouteDefinition doThingRoute( db::DBContext& db )
{
    return http::RouteDefinition {
        http::RouteMetadata { proxygen::HTTPMethod::GET, "/foo/{id}", { "id" }, { apiAuthValidation( db ) } },
        [ &db ]( http::RequestContext context ) { return doThing( db, std::move( context ) ); }
    };
}
```

`apiAuthValidation(db)` (`httpapi/apiAuth.hpp`) is the auth check, attached via `RouteMetadata`'s validations list
rather than a middleware/filter class. Bad input returns `http::Response::error(400, message)` inline; there is no
shared `createBadRequest`-style helper family in the replacement stack.

Handler JSON is built with simdjson (`simdjson::builder::string_builder` for output, `simdjson::dom::parser` or
`http::ValidatedJson` for input), not jsoncpp — check field types with simdjson's own error codes before dereferencing.
`Json::Value` (jsoncpp) still appears in a few places that predate or sit outside the HTTP layer (e.g. job results,
Hydrus response shaping); prefer simdjson for anything new in `httpapi/`.

### Coroutine types and executors

`runtime::Task<T>` (`IDHAN/include/idhan/runtime/Task.hpp`) is `folly::coro::Task<T>` and is the primary coroutine type
across the replacement stack (`http::HTTPTask<T>` is the same type). A bare `folly::coro::Task` cannot be `co_await`ed
directly except from inside another `folly::coro::Task` — its promise's `await_transform` is what supplies the executor.
Awaiting one from a hand-rolled coroutine type (e.g. `JobTask`, the job-runtime coroutine) needs an explicit
`co_await folly::coro::co_withExecutor( executor, someTask() )`; grab the executor via
`co_await folly::coro::co_current_executor` inside a `runtime::Task`, or from whatever owns the calling context
otherwise (e.g. `JobRuntime::executor()`).

`IDHANTask<T>` (`threading/IDHANTask.hpp`) is a separate, older lazy coroutine type with no executor concept at all —
`co_await`ing it just resumes it synchronously inline. It has its own `operator co_await()`, so it can be awaited from a
`runtime::Task` freely (folly's executor-propagation wraps it like any other awaitable), but it must never itself
`co_await` a `runtime::Task` — there's no synchronous bridge back. `modules::RemoteModule`'s methods (`embed`,
`parseMime`, `createThumbnailFile`, ...) are `IDHANTask`-based for this reason.

**Lazy-coroutine pitfall**: `folly::coro::Task` (like the old `drogon::Task` before it) has a suspended
`initial_suspend`, so a coroutine stored in a vector for `folly::coro::collectAllRange` only starts running once
collected. Never use a capturing lambda as such a coroutine — the closure is destroyed at the end of the loop iteration
and the body reads dead captures (use-after-free). Use a captureless lambda/free function and pass all state as
parameters; parameters are copied into the coroutine frame, captures are not.

### File storage (clusters)

Files are stored in on-disk "clusters" managed by `filesystem::ClusterManager` (`IDHANServer/src/filesystem/clusters/`). A cluster maps a `ClusterID` to a directory path. Files are addressed by their `SHA256` hash and `ClusterID`. Cluster assignment and I/O go through `ClusterManager`; never access file paths directly. I/O uses io_uring via `filesystem/io/IOUring.hpp`.

### Module system

`IDHANModules/include/` defines three plugin interfaces: `MetadataModule.hpp`, `ThumbnailerModule.hpp`,
`GeneratorModule.hpp`. New modules implement `ModuleBase` (`IDHANModules/include/ModuleBase.hpp`) and export a factory
with `FGL_EXPORT`. The `ModuleTypeFlags` bitmask declares which interfaces a module implements — one flag per module in
practice, since `handleableMimes()` would be ambiguous across two interface bases.

**Modules never run inside the server.** Each module library is hosted by its own `IDHANModuleRunner` process, so a
module that leaks, corrupts its heap, or crashes takes only that process with it. Premade backends are one library
each — `IDHANVips`, `IDHANFFmpeg`, `IDHANPsd`, `IDHANArchive` — in `build/bin/modules/`.

- **Residency** — a module returns `SINGLE_RUN` (fresh process per call, total leak immunity; the default) or
  `PERSISTENT` from `residency()`. A library is persistent if any of its modules asks for it. Persistent workers are
  still retired on an RSS ceiling or idle timeout, so a leak is bounded rather than merely isolated.
- **Interrogation** — at startup `ModuleLoader` runs `IDHANModuleRunner --describe` per library and registers the
  returned manifest. A library that fails to load, hangs, or exports the wrong symbols is logged and skipped; it does
  not stop the server. Modules are addressed by `(library, module_index)` — the index in the factory's returned vector.
  Names are never used for routing. Every worker re-announces its manifest and the pool compares a signature, so a `.so`
  rebuilt under a running server fails loudly instead of dispatching to a stale index.
- **File data** — files reach modules as a `ModuleFile` handle (`size()` + `read(span, offset)`), never as a buffer.
  Call sites build one with `filesystem::openRecordInput`; `modules::CallInput` decides how it travels. Normally that
  is an io_uring restricted to reading that one file, registered so the worker cannot name what it is reading and no
  copy is made; where io_uring is unavailable it falls back to a sealed anonymous `memfd` (`ipc::Blob`), which costs a
  whole-file copy. Modules never learn which they got. There is deliberately no `readAll()` — a module that needs the
  file contiguous allocates that itself, so the cost is visible where it is incurred. Generator output goes the other
  way through `ModuleSink`, written straight into the memory that carries it back.
- **Calling** — `ModuleLoader::getParserFor` / `getThumbnailerFor` / `getGeneratorsFor` return `RemoteModule` proxies
  whose methods are coroutines: `co_await parser->parseFile( call_data )`.
- **Cross-module calls** — `ModuleCallbacks` (`thumbnail`, `generate`, `probe`) still work; they marshal back to the
  server, which re-dispatches. Nesting depth travels in the call and is bounded by the server (
  `modules.max_call_depth`), because the recursion crosses process boundaries and no per-process counter can see it.
- **Threading** — a worker has an IO thread that never runs module code plus a work pool. `threadSafe()` is
  load-bearing: modules returning false are serialised behind a per-module lock. The pool is what lets
  `ArchiveThumbnailer` → `generate` → `ArchiveGenerator` (same library, same process) complete without deadlocking.
- **Backlog, not deadlines** — a worker queues calls and serves them `pool_threads` at a time, so a call can wait as
  long as the backlog ahead of it. There is no per-call timeout: waiting costs a suspended coroutine and no thread,
  and a deadline could not tell a queued call from a wedged one. Heartbeats are the only watchdog, and they ask only
  whether the process still exists — a worker answers them from its IO loop, which never runs module code. Both ends
  of the channel queue their outbound frames (`ipc::FrameWriter`); neither ever writes straight to the socket, so a
  slow reader cannot wedge the writer or drop a frame on `EAGAIN`.

Platform-specific IPC lives in `IDHANModules/ipc/src/linux/` behind neutral headers (`Blob`, `Frame`, `UniqueFd`). *
*Modules are Linux-only**; a Windows backend would add `ipc/src/windows/` without changing callers.

### Job system

Long-running work is broken into `JobTask` coroutines (`IDHANServer/src/jobs/`) run by the in-memory `JobRuntime` singleton on a dedicated trantor event-loop thread pool (25% of hardware threads, min 2; `server.job_threads` config). Jobs are deliberately **not** persisted to the DB: IDs are process-local and reset on restart. `queueJob(task, name)` enqueues and returns a `JobContext`; endpoints respond immediately with the `job_id` and clients poll `/jobs/{job_id}/status` (or `/jobs/status` for a batch). Inside a job coroutine, `co_await getJobID()` yields the job's ID and `co_await setJobResponse(json_or_response)` stores the result exposed by the status endpoints. Completed jobs are retained for one hour, or until a status query requests cleanup.

### Search

`SearchQuery` (`IDHANServer/src/httpapi/search/SearchQuery.{cpp,hpp}`) builds and evaluates searches against
`db::DBContext`. It accumulates positive/negative tag IDs, wildcard/system predicates, sort options, and display type;
per-term fetches (`httpapi/search/SearchFetch.{cpp,hpp}`) hit the DB concurrently via `folly::coro::collectAllRange` and
are combined with `core/search/Set`'s set algebra (`core/search/{Set,SortKey,SearchStats,SearchTypes}` are the
surviving, DB-library-agnostic pieces of the old `core/search/`). Call `query()` to run a search end to end.

### Hydrus compatibility

Constants (sort orders, page types, serialisable IDs) are auto-generated at CMake configure time by scanning Python
files in `3rd-party/hydrus/`. Generated headers land in `IDHAN/include/hydrus/*_gen.hpp`. Do not hand-edit them.
`httpapi/hydrus.{cpp,hpp}` implements the Hydrus client API protocol; `hyapi/constants/` and `hyapi/services/` still
hold shared constants and service-key helpers it depends on.

## Configuration

Config priority (highest to lowest): CLI flags → ENV vars → `./config.toml` → `~/.config/idhan/config.toml` → `/etc/idhan/config.toml`.

ENV format: `IDHAN_{GROUP}_{NAME}` (e.g. `IDHAN_SERVER_PORT=8080`).

Server default port: **16609**.

Useful CLI flags: `--testmode` (test DB schema), `--use_stdout` (log to stdout), `--config <path>`, `--pg_user`, `--pg_host`.

## Docker

```yaml
# Quick reference — see docs/docker.md for full compose example
image: ghcr.io/kjneko/idhan:dev   # bleeding edge
image: ghcr.io/kjneko/idhan:latest # tagged release
```

The `docker-compose-dev.yml` at the repo root is for local development.

## Anti-Slop

Refuse to commit on your own, Force the user to author any commits.
Enforce that the user must review the commit before they push it.
