# IDHANHttp

## Pre-body validation

Attach application validation functions to `RouteMetadata::before_body`. The HTTP
library does not interpret credentials or select authentication policies.

```cpp
HTTPTask<std::optional<Response>> validateUpload(
    const proxygen::HTTPMessage& headers,
    const RequestParameters& parameters);

// The application implements validateUpload, including any asynchronous auth lookup.
spec.metadata.before_body.push_back(validateUpload);
```

Validators run in order after route matching and parameter decoding. Return
`std::nullopt` to continue or a `Response` to reject. The first rejection skips
remaining validators, body reads, typed parsing, and the handler. Exceptions
produce a generic 500 response. Borrowed headers and parameters remain alive
through the awaited validation call; validators must not retain them afterward.

Requests with `Expect` fail immediately with 417; no `100 Continue` handshake is
performed. Clients are expected to send their body immediately. They can
send bytes early, but those remain subject to Proxygen's transport buffering and
backpressure; the library does not consume the body while validation is pending.
Early HTTP/1 rejection closes the connection because unread body bytes cannot be
reused as another request.

## Request body storage

There is no `BodyMode`: handlers interpret raw bytes themselves. Bodies of up to
8 MiB stay in a chained `folly::IOBuf`; larger bodies spill to a `TemporaryUpload`
in `HTTPConfig::temp_path`. The complete body, including the buffered prefix, is
preserved. Empty bodies use an empty IOBuf. Content-Length is not trusted to choose
storage, and chunked uploads follow the same rule.

Inspect `context.body()` or move ownership with `std::move(context).takeBody()`.
JSON, multipart, and Content-Encoding interpretation belong to the handler.
File creation and positional writes run on an owned worker executor, with each
write awaited before receiving another chunk. Temporary files are unlinked and
close automatically. Their SHA-256 covers the raw body, not extracted file parts.

## Echo integration fixture

`IDHANHttp/test/EchoServer.cpp` is a cleartext HTTP/1.1 integration fixture. It
uses `HTTPServer`, `RouteRegistry`, `RequestContext`, and `Response`; it does
not implement a Proxygen handler. It registers `POST /echo`, preserving request
bytes; `GET /version`, which returns the build version as JSON; and `GET /{id}`
which returns the decoded `id` capture as plain text.

```sh
cmake --build build/debug --target IDHANHttpTests
./build/debug/bin/IDHANHttpTests "The echo server exposes echo and version endpoints"
```

The test starts on an ephemeral port and sends requests through `CurlHelper`.

The Proxygen layer provides lazy coroutine routes for metadata and regular files. It
has no Drogon or database API dependency. Existing IDHANServer controllers remain
on Drogon until their application logic is migrated.

```cpp
http.addRoute("/metadata", [](const proxygen::HTTPMessage& request, const folly::IOBuf*) -> idhan::coro::Task<idhan::http::RouteResponse> {
    // Validate request.getQueryParam("record_id") before looking up metadata.
    Json::Value metadata;
    metadata["status"] = "available";
    auto response = idhan::http::RouteResponse::json(metadata);
    response.message.getHeaders().set("Cache-Control", "no-store");
    co_return response;
});
```

`addRoute(path, handler, method)` defaults to GET. Paths are exact; query strings
are available on the request and do not participate in matching. GET also handles
HEAD unless an explicit HEAD handler exists. Different methods can share a path;
unsupported paths or methods return 404. `removeRoute(path)` removes all methods.
Handlers receive a borrowed IOBuf chain (possibly null), not necessarily one
contiguous buffer. Metadata request bodies are limited to 8 MiB; larger requests
return 413 after receipt. This is not a large-upload API.

For a file, return `RouteResponse::fromFile(std::move(file), mime_type)` with an
owning `folly::File`. Obtain that descriptor through the storage layer; do not map
request paths directly to filesystem paths. The response keeps the descriptor
alive until transmission completes and uses positional reads, so it does not
change the descriptor's current offset. Set Content-Disposition and Cache-Control
through `response.message.getHeaders()` when needed. File bytes should remain immutable during
transmission; premature EOF aborts the response.

Files are read on an owned two-thread Folly CPU executor, one 64 KiB chunk at a time, and sent on
the HTTP event loop. Egress backpressure pauses subsequent reads. Disconnects defer
handler destruction until an outstanding read completes. HEAD sends the same
length and metadata without reading the body. Single byte ranges support closed,
open-ended, and suffix ranges; unsatisfiable ranges return 416. Multiple ranges,
unknown range units, and If-Range requests fall back to a full 200 response. HEAD
ignores Range. Malformed ranges also fall back to the full response. A nonzero
suffix range on an empty file returns the full empty response. The header-only
`helpers/parseByteRange.hpp` parser is constexpr and has compile-time coverage.
Automatic compression is disabled so file range offsets and
Content-Length describe the bytes actually sent.

`RouteResponse` holds Proxygen's `HTTPMessage` directly and an optional Folly
`IOBuf` chain or open file. Set status and headers through `response.message`;
there is no separate HTTP status/header representation to convert. Buffered
bodies transfer ownership directly to Proxygen without another copy.

The HTTP layer owns Content-Length and Transfer-Encoding. JSON responses use
JsonCpp and application/json. An exception before headers becomes an empty 500;
a failure after headers aborts the stream without exposing exception details.

Route handlers return `idhan::coro::Task<RouteResponse>` and start when dispatched
after request receipt. They may suspend with `co_await` and finish with `co_return`.
Request data and route captures stay alive until completion, including after a
client disconnect. A disconnected request discards the completed response. Task
cancellation is not implemented, so pending operations must eventually complete.
Response delivery always returns to the HTTP loop; route awaitables control where
the route itself resumes. Do not put blocking libpqxx queries in route bodies. The database replacement will use libpqxx
directly with
connections owned by database workers and an asynchronous completion/coroutine
bridge back to the HTTP loop. No database pool, WebSocket,
parameterized-path router, or static-directory fallback is implemented here yet.

Run `cmake --build build/debug --target IDHANHttpTests` and
`./build/debug/bin/IDHANHttpTests` to validate the layer.
