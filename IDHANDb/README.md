# IDHANDb

`IDHANPQContext` is a direct libpq pool using the same `folly::coro::Task`
and `folly::EventBase` facilities as Proxygen's coroutine HTTP handlers.
It uses libpq directly and has no HTTP-layer or libpqxx dependency.

Initialize Folly once in `main()` with `folly::Init`. Construct a context with
an EventBase that will keep running until all submitted tasks finish:

```cpp
idhan::db::IDHANPQContext db {
    event_base,
    "hostaddr=127.0.0.1 dbname=idhan-db user=idhan password=idhan",
    8,    // maximum connections for this context
    128   // maximum additional pending queries
};
```

Connections open lazily and are reused. A shared context can be called from
multiple HTTP executors; libpq operations run on its EventBase and Folly returns
the awaiting coroutine to its caller's executor. Alternatively, use a context
per HTTP EventBase and divide the total database connection budget among them.
Returned tasks retain the pool even if the context handle is destroyed. The
borrowed EventBase must still outlive those tasks. Destroying a lazy task before
starting it submits nothing.

Use `hostaddr` for TCP connections (optionally together with `host` for TLS name
verification), or an explicit Unix socket directory. libpq can perform blocking
DNS resolution when given only a hostname; this layer does not provide a DNS
resolver. Socket waits during connection establishment and queries are
nonblocking EventBase registrations. The query deadline also governs connection
establishment; libpq's `connect_timeout` does not govern `PQconnectPoll`.

## Binary parameters

`PQParameter` stores text in `std::string`, binary data in `std::vector<std::byte>`,
and SQL NULL as `std::monostate`. It exposes libpq's `Oid` and format codes.
`PQsendQueryParams` receives types, values, explicit lengths, and formats. Each
parameter may independently use text (0) or binary (1). Binary data may contain
embedded NUL bytes and is sent without hex or decimal encoding.

```cpp
using idhan::db::PQParameter;
namespace pq_oid = idhan::db::pq_oid;

std::vector<PQParameter> parameters;
parameters.push_back(PQParameter::int64(123456789));
parameters.push_back(PQParameter::binary(pq_oid::bytea, hash_bytes));
parameters.push_back(PQParameter::null(pq_oid::bytea, 1)); // binary SQL NULL

auto result = co_await db.query(
    "SELECT $1::int8, octet_length($2::bytea), $3::bytea",
    std::move(parameters));
```

`int32` and `int64` encode signed PostgreSQL integers in network byte order using
4 and 8 bytes. Arbitrary `binary(type_oid, bytes)` values must already use that
PostgreSQL type's binary wire format. Raw native structs, native-endian integers,
and the binary format of one type cannot be substituted for another type's
format. Text parameters reject embedded NULs. Empty binary values and SQL NULL
are distinct. The vector overload accepts moved storage; the span overload copies
bytes so lazy coroutine execution cannot outlive borrowed input.

`PQOid.hpp` defines the built-in scalar and array OIDs used by IDHAN. This includes
`text_array`, `bytea_array`, all integer and floating-point arrays, UUID arrays,
JSON/JSONB arrays, numeric arrays, and date/time arrays. Array parameters use the
same API as scalar parameters. Text array input can use PostgreSQL's array literal
format, for example `PQParameter::text(R"({"a","b"})", pq_oid::text_array)`.
Binary array input must use PostgreSQL's binary array wire format, including its
array header and per-element lengths.

Results default to text. Pass `1` as the fourth query argument to request binary
fields for every result column:

```cpp
auto result = co_await db.query("SELECT 42::int4", {}, std::chrono::seconds(30), 1);
// Decode according to PQftype/PQfformat. Use PQgetlength, not strlen.
```

`PQResult` owns `PGresult` with a `PQclear` deleter. Use libpq's result APIs directly;
field pointers remain valid until that result is destroyed, independently of the
pool connection's next query.

## Failure and scope

The default deadline is 30 seconds, measured from task execution, including the
executor queue, pool wait, connection establishment, and query. Supported values
are positive durations up to 24 hours. The pending limit bounds admitted queries;
callers must also bound task creation and request payload sizes.

Errors throw `PQError`, retaining SQLSTATE for server errors. Invalid arguments
throw `std::invalid_argument`; Folly cancellation throws `folly::OperationCancelled`.
Timeouts, cancellation, and database errors discard the connection and release
its pool slot. A subsequent query opens a replacement. Queries are never retried:
a disconnect or cancellation can leave a write's commit outcome uncertain.
Closing a connection does not guarantee immediate interruption of server-side
execution; configure PostgreSQL `statement_timeout` where server-side limits are
required.

This initial API executes one statement per query in autocommit mode. Explicit
transactions and COPY are rejected and their connections discarded. Session-changing
SQL (SET, LISTEN, session locks, temporary tables) is outside the supported pool
contract: no session reset is performed between successful queries. Dedicated
transaction leases, COPY streaming, prepared-statement caching, and pipeline mode
are not implemented. Existing IDHANServer database call sites have not yet been
migrated to this context.

## Tests

```sh
cmake --build build/debug --target IDHANDbTests
./build/debug/bin/IDHANDbTests '[pq]'
IDHAN_PQ_TEST_CONNINFO='hostaddr=127.0.0.1 dbname=idhan-db user=idhan password=idhan' \
    ./build/debug/bin/IDHANDbTests '[.pq-integration]'
```

Integration tests are opt-in and use only transient sessions and SELECT queries;
they do not require the IDHAN schema. They cover binary/text/NULL binding, binary
results, overlapping connections, caller-executor resumption, pool bounds,
deadlines, cancellation, errors, and connection reuse.
