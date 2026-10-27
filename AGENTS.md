# IDHAN Agent Guide

## Project overview

IDHAN is a C++26 media-management and archival server with a booru-compatible
tagging model and Hydrus Network compatibility. It uses CMake, PostgreSQL,
Proxygen/Folly, libpq (libpqxx in legacy code/tests), Qt6, and a React WebUI.

## Repository layout

- `IDHAN/`: shared types, utilities, and generated Hydrus constants.
- `IDHANServer/`: Proxygen/Folly HTTP server, API handlers, database logic, jobs, and
  filesystem clusters.
- `IDHANDb/`: coroutine PostgreSQL access, bindings, and typed codecs.
- `IDHANHttp/`: Proxygen HTTP transport, routing, and validation.
- `IDHANDownloader/`: downloader runtime, parsers, and generated API clients.
- `IDHANModules/`: module interfaces and isolated module runners/backends.
- `IDHANMigration/`: numbered PostgreSQL schema migrations.
- `IDHANClient/`: Qt6 REST client library.
- `tools/HydrusImporter/`: Qt GUI Hydrus database importer.
- `tests/`: Catch2 suites and database/API fixtures.
- `IDHANWeb/`: React WebUI.
- `dependencies/` and `3rd-party/`: vendored dependencies; avoid modifying
  them unless the task explicitly requires it.

## Git workflow

- Target `dev` for features and fixes; `master` is release-only.
- Do not create commits. The user must review and author commits before pushing.
- Preserve unrelated working-tree changes.

## Build and test

Initialize submodules before configuring a new checkout:

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=System
cmake --build build -j"$(nproc)" --target IDHANServer
```

Prefer an existing configured build directory when available, for example:

```sh
cmake --build build/debug --target IDHANServer
```

Enable and run tests with PostgreSQL available at
`dbname=idhan-db user=idhan password=idhan host=localhost`:

```sh
cmake -S . -B build -DBUILD_IDHAN_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Tests use Catch2, with separate targets such as `IDHANDBTests`, `IDHANAPITests`,
and `IDHANHttpTests` (see `tests/CMakeLists.txt`). PostgreSQL settings can be
overridden with `IDHAN_TEST_PG_{HOST,PORT,DB,USER,PASSWORD}`; see
`tests/api/README.md` and `docs/database-initialization.md` for setup.
Run a focused case or tag against an existing binary when practical:

```sh
./build/bin/IDHANDBTests "[tag]"
# or: ./build/bin/IDHANAPITests "test case name"
```

Build output is in `<build-dir>/bin/`. Options and defaults are declared in
`CMakeLists.txt`; tests default OFF, server/client/importer/WebUI default ON.

## C++ conventions

- Do not use anonymous namespaces. Place helpers in the appropriate named
  project namespace and assume names within our namespaces do not collide.
- Use the underlying library's types and APIs directly where they fit. Avoid
  duplicating their representations just to convert back at the boundary;
  wrappers should add necessary application behavior or ownership only.
- Prefer named functions or member functions for substantial or reusable logic.
  Use lambdas sparingly for short, local callbacks; move long or nested callback
  bodies into named helpers. Keep captures minimal and ownership explicit.
- Prefer returning owned values over output parameters or reference returns.
  Use `std::optional<T>` when a result may be absent and `std::expected<T, E>`
  when failure details matter. Return a struct or tuple for multiple results.
  Keep reference returns only for intentional borrowed access with clear lifetime
  requirements; preserve signatures required by external interfaces.
- Name `std::expected` results with an `_e` suffix. Check `has_value()` before
  accessing the result, then bind a named reference to `.value()` when it will
  be used more than once. Prefer this over repeated `*` dereferences, especially
  for nested `expected` and `optional` values.
- Keep one class per header unless additional classes are tightly integrated or
  used only by that class.
- Use the distinct ID aliases in `IDHAN/include/IDHANTypes.hpp`, not raw
  integer types. Use `SHA256` for record hashes.
- Do not use capturing lambdas for lazy `runtime::Task` coroutines collected by
  Folly; use a captureless lambda with explicit parameters.
- Declare local variables as close as possible to their first use.
- Prefer a separate declaration and `if` statement over an `if` initializer;
  do not chain setup and branching in one condition.
- Prefer small sequential guard clauses over compound or multi-line `if`
  conditions.
- Leave a blank line after every `if` body before the next statement, including
  single-statement bodies.
- Separate consecutive guard branches with two blank lines after an early
  `return`, so each validation step remains visually distinct.
- Place an adjacent comment on every regular expression describing the value
  it matches. For URL-related expressions, include a concrete valid URL.
- Do not add `//!` comments that merely restate self-explanatory names or code.
  Reserve documentation comments for non-obvious contracts, behavior, invariants,
  constraints, or rationale.

## HTTP and coroutines

- Native routes live in `IDHANServer/src/httpapi/`, namespace `idhan::httpapi`.
  Handlers return `http::HTTPTask<http::Response>`. Register `http::RouteDefinition`
  with route metadata and `apiAuthValidation(db)` for authenticated endpoints.
- Use the existing parameter declarations and `readValidatedJsonBody` helpers.
  New HTTP JSON uses simdjson; check types/error codes before accessing values.
  Return errors with `http::Response::error(status, message)`; the old
  `createBadRequest` helper family is gone. Where JsonCpp remains, validate types
  before `as*()` and construct arrays as `Json::Value { Json::arrayValue }`.
- `runtime::Task<T>` and `http::HTTPTask<T>` alias `folly::coro::Task<T>`.
  Fallible helpers can return a task of `std::expected<T, E>`.
- Await Folly tasks from another Folly task, or supply an executor explicitly
  with `folly::coro::co_withExecutor`. The older `IDHANTask` has no executor;
  it can be awaited from a Folly task but must not directly await one.

## Database and storage

- Never apply persistent schema changes directly to the live database, including
  indexes, tables, functions, and triggers. Implement them as migrations and let
  the normal migration process apply them. A request to implement a schema change
  does not authorize applying it manually to the live database.
- Temporary, non-destructive changes solely for testing are allowed, provided
  they are rolled back or cleaned up after testing and leave no persistent changes
  to the live schema or existing data.
- Add migrations under `IDHANMigration/src/<type>_<object>/NNN-description.sql`,
  one object per file. Use the globally highest number plus one, zero-padded;
  numbers must be unique across directories. Startup runs them in numeric order.
  See `IDHANMigration/src/README.md` and
  `IDHANMigration/cmake_modules/GenerateMigrations.cmake`.
- Prefer typed domain helpers for reusable queries, including ordinary CRUD,
  existence checks, and stable lookups even when they currently have only one
  caller. Search for an existing helper before writing SQL at a call site.
- Keep SQL inline only when it is genuinely one-off or specialized, such as a
  report, endpoint-specific aggregate, transaction-local statement, or dynamic
  search query. Helpers should decode rows into domain types without absorbing
  caller-specific HTTP responses or transaction policy.
- Tag aliases, siblings, and parents are resolved by PostgreSQL triggers and
  views. Do not reimplement that resolution in application code.
- Use `db::DBBindings` for runtime PostgreSQL parameters, including typed arrays;
  let the database layer encode values instead of building array literals.
- Access stored files through `filesystem::ClusterManager` and
  `filesystem::openRecordInput`; do not construct cluster paths directly.

## Modules and generated code

- Modules run in separate `IDHANModuleRunner` processes. Use the defined module
  interfaces and `RemoteModule` coroutine proxies; do not load or invoke module
  libraries directly in the server.
- Hydrus constants are generated from `3rd-party/hydrus/` at CMake configure
  time (see `IDHAN/CMakeLists.txt`). Never edit `IDHAN/include/hydrus/*_gen.hpp`
  or `IDHAN/include/hydrus*_gen.hpp` manually.

## Architecture references

- Database queries: `IDHANDb/include/idhan/db/DBContext.hpp`; decode through
  `.rows<T...>()`, or use `.result()` when no rows are needed.
- Search: `IDHANServer/src/httpapi/search/`; jobs: `IDHANServer/src/jobs/`.
- Module proxies and worker pools: `IDHANServer/src/modules/`; interfaces and
  IPC: `IDHANModules/`. Keep module file I/O behind `ModuleFile`/`ModuleSink`.
- Hydrus API: `IDHANServer/src/httpapi/hydrus.cpp`.

## Configuration

- Configuration priority is CLI flags, environment variables, local
  `config.toml`, user config, then system config. `--config <path>` replaces
  the file search; CLI and environment still take priority. See
  `IDHANServer/src/Config.hpp` for platform paths.
- Environment variable names use `IDHAN_{GROUP}_{NAME}`. The default server port
  is 16609.
- Useful server flags include `--testmode`, `--use_stdout`, and `--config`.
