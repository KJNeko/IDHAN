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

PostgreSQL accessed via libpqxx (direct) and Drogon's ORM/async client (`drogon::orm::DbClientPtr`, aliased as `idhan::DbClientPtr`). API handlers use Drogon coroutines (`drogon::Task<>`, `IDHANTask<>`).

**Schema migrations** live in `IDHANMigration/src/` as `N-tablename.sql`. Files are executed in ascending numeric order at server startup. When adding a migration, name it `(N+1)-tablename.sql`.

`db/drogonArrayBind.hpp` provides helpers for binding C++ vectors as PostgreSQL array parameters (e.g. `UNNEST($1::integer[])`). clangd flags this include as unused — it isn't; it supplies template specializations resolved implicitly at the `execSqlCoro` call site. Don't remove it.

### API handler pattern

All handlers are Drogon `HttpController` subclasses in `namespace idhan::api`. Routes are registered inside `METHOD_LIST_BEGIN` / `METHOD_LIST_END` using `ADD_METHOD_TO`. The last argument to `ADD_METHOD_TO` must be `IDHANAPIAuthName` to attach the auth middleware filter. Handler methods return `drogon::Task<drogon::HttpResponsePtr>`.

```cpp
class FooAPI : public drogon::HttpController<FooAPI>
{
    drogon::Task<drogon::HttpResponsePtr> doThing(drogon::HttpRequestPtr request, RecordID id);
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FooAPI::doThing, "/foo/{id}", drogon::Get, IDHANAPIAuthName);
    METHOD_LIST_END
};
```

Response helpers (all log a warning and return the right status code) are in `api/helpers/createBadRequest.hpp`:
- `createBadRequest(...)` → 400
- `createNotFound(...)` → 404
- `createInternalError(...)` → 500
- `createConflict(...)` → 409

All helpers accept `std::format`-style format strings.

Handler JSON conventions: check `isString()`/`isArray()`/`isIntegral()` before calling `as*()` — jsoncpp's `asString()` throws on the wrong type, which surfaces as a 500 where a `createBadRequest` 400 belongs. Initialize array responses with `Json::Value json { Json::arrayValue }`; a default-constructed `Json::Value` is null until first append, so empty results serialize as `null` instead of `[]`.

### Coroutine types and error propagation

`IDHANTask<T>` (`threading/IDHANTask.hpp`) is the primary coroutine type. `ResponseTask` and `Request` aliases are defined in `api/helpers/ResponseCallback.hpp`.

For handlers that call helpers which can fail, use `ExpectedTask<T>` = `IDHANTask<std::expected<T, drogon::HttpResponsePtr>>` and the `return_unexpected_error(var)` macro, which co_returns the error response if `var` holds an unexpected:

```cpp
ExpectedTask<RecordID> helper(DbClientPtr db);

ResponseTask myHandler(Request req) {
    const auto result = co_await helper(db);
    return_unexpected_error(result);  // propagates 4xx/5xx if failed
    co_return okResponse(result.value());
}
```

**Lazy-coroutine pitfall**: `drogon::Task` has a suspended `initial_suspend`, so a coroutine stored in a vector for `drogon::when_all` only starts running after the loop that created it has finished. Never use a capturing lambda as such a coroutine — the closure is destroyed at the end of the loop iteration and the body reads dead captures (use-after-free). Use a captureless lambda and pass all state as parameters; parameters are copied into the coroutine frame, captures are not.

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

`SearchBuilder` (`IDHANServer/src/core/search/`) builds PostgreSQL queries dynamically. It accumulates positive/negative tag IDs, system predicates (size, dimensions, duration, etc.), sort options, and required JOINs, then emits a single SQL string. `$1` is always bound to an array of `tag_domain_ids`. Call `construct()` to get the SQL, or `query()` to execute it directly.

### Hydrus compatibility

Constants (sort orders, page types, serialisable IDs) are auto-generated at CMake configure time by scanning Python files in `3rd-party/hydrus/`. Generated headers land in `IDHAN/include/hydrus/*_gen.hpp`. Do not hand-edit them. The `hyapi/` subdirectory in the server implements the Hydrus client API protocol.

## Configuration

Config priority (highest to lowest): CLI flags → ENV vars → `./config.toml` → `~/.config/idhan/config.toml` → `/etc/idhan/config.toml`.

ENV format: `IDHAN_{GROUP}_{NAME}` (e.g. `IDHAN_SERVER_PORT=8080`).

Server default port: **16609**.

Useful CLI flags: `--testmode` (test DB schema), `--use_stdout` (log to stdout), `--config <path>`, `--pg_user`, `--pg_host`.

## Docker

```yaml
# Quick reference — see docs/docker.md for full compose example
image: git.futuregadgetlabs.net/kj16609/idhan:dev   # bleeding edge
image: git.futuregadgetlabs.net/kj16609/idhan:latest # tagged release
```

The `docker-compose-dev.yml` at the repo root is for local development.
