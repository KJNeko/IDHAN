# IDHAN Agent Guide

## Project overview

IDHAN is a C++23 media-management and archival server with a booru-compatible
tagging model and Hydrus Network compatibility. It uses CMake, PostgreSQL,
Drogon, libpqxx, Qt6 (client/importer), and a React WebUI.

## Repository layout

- `IDHAN/`: shared types, utilities, and generated Hydrus constants.
- `IDHANServer/`: Drogon HTTP server, API handlers, database logic, jobs, and
  filesystem clusters.
- `IDHANModules/`: module interfaces and isolated module runners/backends.
- `IDHANMigration/`: numbered PostgreSQL schema migrations.
- `IDHANClient/`: Qt6 REST client library.
- `tools/HydrusImporter/`: Qt GUI Hydrus database importer.
- `tests/`: GoogleTest suite and database fixtures.
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
cmake -DCMAKE_BUILD_TYPE=System -B build
cmake --build build -j"$(nproc)" --target IDHANServer
```

Prefer an existing configured build directory when available, for example:

```sh
cmake --build build/debug --target IDHANServer
```

Enable and run tests with PostgreSQL available at
`dbname=idhan-db user=idhan password=idhan host=localhost`:

```sh
cmake -DBUILD_IDHAN_TESTS=ON -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build
```

Tests use Catch2. Run a focused test case or tag against an existing test
binary when practical:

```sh
./build/bin/IDHANDBTests "[tag]"
# or: ./build/bin/IDHANAPITests "test case name"
```

## C++ and API conventions

- Use the distinct ID aliases in `IDHAN/include/IDHANTypes.hpp`, not raw
  integer types. Use `SHA256` for record hashes.
- API controllers are `drogon::HttpController` classes in `namespace idhan::api`.
  Register routes with `ADD_METHOD_TO` and include `IDHANAPIAuthName` as the
  final argument.
- Validate JSON types with `isString()`, `isArray()`, and `isIntegral()` before
  `as*()` calls. Use `Json::Value json { Json::arrayValue }` for array responses.
- Return API errors through the helpers in `api/helpers/createBadRequest.hpp`.
- For fallible coroutine helpers, prefer `ExpectedTask<T>` and propagate errors
  with `return_unexpected_error(...)`.
- Do not use capturing lambdas for lazy `drogon::Task` coroutines passed to
  `drogon::when_all`; use a captureless lambda with explicit parameters.
- Declare local variables as close as possible to their first use.
- Prefer a separate declaration and `if` statement over an `if` initializer;
  do not chain setup and branching in one condition.
- Prefer small sequential guard clauses over compound or multi-line `if`
  conditions.
- Separate consecutive guard branches with two blank lines after an early
  `return`, so each validation step remains visually distinct.

## Database and storage

- Add migrations only as the next sequentially numbered SQL file in
  `IDHANMigration/src/`; they run in ascending order at server startup.
- Tag aliases, siblings, and parents are resolved by PostgreSQL triggers and
  views. Do not reimplement that resolution in application code.
- Use `db/drogonArrayBind.hpp` for PostgreSQL array bindings; its apparently
  unused include supplies required template specializations.
- Access stored files through `filesystem::ClusterManager` and
  `filesystem::openRecordInput`; do not construct cluster paths directly.

## Modules and generated code

- Modules run in separate `IDHANModuleRunner` processes. Use the defined module
  interfaces and `RemoteModule` coroutine proxies; do not load or invoke module
  libraries directly in the server.
- Generated Hydrus headers in `IDHAN/include/hydrus/*_gen.hpp` are produced at
  CMake configure time. Never edit them manually.

## Configuration

- Configuration priority is CLI flags, environment variables, local
  `config.toml`, user config, then system config.
- Environment variable names use `IDHAN_{GROUP}_{NAME}`. The default server port
  is 16609.
- Useful server flags include `--testmode`, `--use_stdout`, and `--config`.
