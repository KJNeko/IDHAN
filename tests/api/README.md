# API tests

The API harness uses libcurl directly. The server uses Proxygen/Folly, and there
is no test-only web framework or event-loop dependency. JSON parsing and writing
use the project's existing JsonCpp dependency.

Build the normal target with tests enabled. Use a separate build directory when
CLion is managing another one:

```sh
cmake -S . -B build/tests -DBUILD_IDHAN_TESTS=ON
cmake --build build/tests --target IDHANAPITests -j4
./build/tests/bin/IDHANAPITests
```

Database-backed cases use an isolated migrated PostgreSQL schema. The defaults
are `host=localhost port=5432 dbname=idhan-db user=idhan password=idhan`;
`IDHAN_TEST_PG_HOST`, `IDHAN_TEST_PG_PORT`, `IDHAN_TEST_PG_DB`,
`IDHAN_TEST_PG_USER`, and `IDHAN_TEST_PG_PASSWORD` select a test server. Required
extensions must already be installed, as described in
[database initialization](../../docs/database-initialization.md).
The server and module runner are built alongside the test executable.
`IDHAN_TEST_SERVER_BINARY` can explicitly select another server binary.

Transport cases need loopback sockets but no PostgreSQL or running IDHAN server:

```sh
./build/tests/bin/IDHANAPITests '[transport]'
```

Every request has its own libcurl easy handle, so concurrent test calls do not
share mutable transfer state. Query names and values are percent-encoded while
preserving repeated parameters. Requests connect directly to loopback without
using environment proxy settings or following redirects. HTTP error statuses
remain responses; connection and deadline failures throw with the request path
and libcurl's diagnostic.

The harness retains binary request/response bytes, parses JSON when possible,
and normalizes response header names to lowercase. Use lowercase names with
`ApiResponse::header`. Interim response headers are discarded before the final
response. JSON and ordinary requests have a 30-second total deadline; octet
uploads have a 60-second deadline. The constructor accepts shorter positive
limits for transport tests. libcurl is initialized once and released after the
clients, and callbacks capture exceptions without unwinding through its C API.

The transport regressions cover repeated/escaped query values, empty values,
authentication headers, JSON escaping, embedded-NUL and non-UTF-8 bytes,
chunked/interim responses, PATCH/DELETE/bodyless POST, HTTP errors, redirects,
timeouts, and refused connections. The API regressions exercise the same client
against the real server, including concurrent calls and large request bodies.
