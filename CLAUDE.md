# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is IDHAN

IDHAN is a C++23 media management and archival server with a booru-compatible tagging model. It is designed for large media collections and has Hydrus Network compatibility baked in (shared constants, Hydrus API endpoint compatibility, HydrusImporter tool).

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

**Key CMake options:**

| Option | Default | Purpose |
|---|---|---|
| `BUILD_IDHAN_SERVER` | ON | Main server |
| `BUILD_IDHAN_CLIENT` | ON | Qt client shared library |
| `BUILD_HYDRUS_IMPORTER` | ON | Qt GUI import tool |
| `BUILD_IDHAN_TESTS` | OFF | GoogleTest suite |
| `BUILD_IDHAN_WEBUI` | OFF | WASM frontend (requires Emscripten + Qt for WASM) |
| `IDHAN_DISABLE_API_AUTH` | OFF | Disable API key checks (dev only) |

## Tests

Tests require a running PostgreSQL instance. `--testmode` forces the server to use the `test` schema instead of `public`.

```bash
cmake -DBUILD_IDHAN_TESTS=ON -B build
cmake --build build -j$(nproc) --target IDHANTests
ctest --test-dir build
# Or run a single test binary directly:
./build/bin/IDHANTests --gtest_filter="SuiteName.TestName" --testmode --use_stdout
```

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

All integer IDs are distinct type aliases (`RecordID`, `TagID`, `TagDomainID`, `ClusterID`, etc.) over `int32_t`/`int16_t`/`int64_t`. Always use these aliases rather than raw integers.

### Tag model

Tags are stored as `namespace:subtag` pairs. The `IDHAN/` library provides `splitTag()`. The server keeps several layers:
- **Raw mappings** — what was directly applied to a record.
- **Active mappings** — after alias, sibling, and parent resolution, surfaced via DB views (`active_tag_mappings_final`).

Tag relationships (aliases, siblings, parents) live in their own tables and are resolved by DB triggers/views rather than application code.

### Database

PostgreSQL accessed via libpqxx (direct) and Drogon's ORM/async client (`drogon::orm::DbClientPtr`, aliased as `idhan::DbClientPtr`). API handlers use Drogon coroutines (`drogon::Task<>`, `IDHANTask<>`).

**Schema migrations** live in `IDHANMigration/src/` as `N-tablename.sql`. Files are executed in ascending numeric order at server startup. When adding a migration, name it `(N+1)-tablename.sql`.

### Module system

`IDHANModules` defines three plugin interfaces: `MetadataModuleI`, `ThumbnailerModuleI`, `GeneratorModuleI`. Premade implementations (vips, FFmpeg, libarchive, PSD) are compiled into `IDHANPremadeModules`, a shared library placed in `build/bin/modules/`. At runtime, `ModuleLoader` `dlopen`s everything in that directory. New modules must implement `ModuleBase` and export a factory via `FGL_EXPORT`.

### Job system

Long-running work (e.g. cluster scan) is broken into `JobTask` coroutines stored in the DB. A job can declare dependencies on other jobs. Statuses: Pending → Started → Completed / Failed / Await Dependency.

### Search

`SearchBuilder` (`IDHANServer/src/core/search/`) builds PostgreSQL queries dynamically. It accumulates positive/negative tag IDs, system predicates (size, dimensions, duration, etc.), sort options, and required JOINs, then emits a single SQL string. `$1` is always bound to an array of `tag_domain_ids`.

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
