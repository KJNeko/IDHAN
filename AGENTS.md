# IDHAN Agent Guide

## Project overview

IDHAN is a C++26 media-management and archival server with a booru-compatible
tagging model and Hydrus Network compatibility. It uses CMake, PostgreSQL,
Proxygen/Folly, libpq and libpqxx, Qt6 (client/importer), and a React WebUI.

## Repository layout

- `IDHAN/`: shared types, utilities, and generated Hydrus constants.
- `IDHANServer/`: Proxygen/Folly HTTP server, API handlers, database logic, jobs, and
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
- Native API routes live in `namespace idhan::httpapi`. Register
  `http::RouteDefinition` values with route metadata and `apiAuthValidation(db)`
  for authenticated endpoints.
- Validate JSON types with `isString()`, `isArray()`, and `isIntegral()` before
  `as*()` calls. Use `Json::Value json { Json::arrayValue }` for array responses.
- Return API errors through the helpers in `api/helpers/createBadRequest.hpp`.
- For fallible coroutine helpers, prefer `ExpectedTask<T>` and propagate errors
  with `return_unexpected_error(...)`.
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

## Database and storage

- Never apply persistent schema changes directly to the live database, including
  indexes, tables, functions, and triggers. Implement them as migrations and let
  the normal migration process apply them. A request to implement a schema change
  does not authorize applying it manually to the live database.
- Temporary, non-destructive changes solely for testing are allowed, provided
  they are rolled back or cleaned up after testing and leave no persistent changes
  to the live schema or existing data.
- Add migrations only as the next sequentially numbered SQL file in
  `IDHANMigration/src/`; they run in ascending order at server startup.
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
- Generated Hydrus headers in `IDHAN/include/hydrus/*_gen.hpp` are produced at
  CMake configure time. Never edit them manually.

## Configuration

- Configuration priority is CLI flags, environment variables, local
  `config.toml`, user config, then system config.
- Environment variable names use `IDHAN_{GROUP}_{NAME}`. The default server port
  is 16609.
- Useful server flags include `--testmode`, `--use_stdout`, and `--config`.
