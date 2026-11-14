# IDHANDb

IDHANDb provides coroutine PostgreSQL access through libpq and Folly. Public
headers live under `include/idhan/db/`; `include/IDHANDB.hpp` declares `DBTask`.

## Context and queries

Initialize Folly at process startup. Construct a `DBContext` with `DBConfig` and
an application executor keep-alive token, then await `start()`. The context owns
its reactor and connection pool; keep the application executor alive until work
has drained. The process supports one `DBContext` instance.

Bind runtime values with `DBBindings`, including typed arrays. Codecs encode
PostgreSQL wire values and decode result columns into domain types:

```cpp
idhan::db::DBBindings bindings;
bindings.bind(std::int64_t { 42 });
auto rows = co_await db.query("SELECT $1::bigint", std::move(bindings))
    .rows<std::int64_t>();
```

`query()` returns a lazy `DBTask`. Await `.rows<T...>()` for typed rows, `.result()`
when no typed rows are needed. Query
errors throw `DBQueryError`; decoding errors retain query and column context.

Use `co_await db.transaction()` for a transaction lease. Queries on that lease
share a transaction connection; explicitly await `commit()` or `rollback()`.
Callers control transaction policy. Await `DBContext::drain()` during shutdown.

`DBConfig` controls normal and transaction connection counts, reactor threads,
statement timeout, and query diagnostics. `DBBindings.hpp`, `DBResult.hpp`, and
their `detail/` headers define codecs and typed row access.

## Tests

```sh
cmake --build build/debug --target IDHANDbTests
./build/debug/bin/IDHANDbTests '[db][decode]'
./build/debug/bin/IDHANDbTests '[postgres]'
```

PostgreSQL integration tests require a running database. The connection tests in
`test/ManagementConnection.cpp` accept `IDHAN_PQ_TEST_CONNINFO`; transaction and
codec tests use `DBConfig` defaults. These tests exercise database connections,
transactions, diagnostics, and typed codecs. The connection tests also exercise
management-role connections directly through `DBConnection`.
