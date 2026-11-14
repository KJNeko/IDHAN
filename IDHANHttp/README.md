# IDHANHttp

## Layout

Public headers live under `include/idhan/http/`; implementations under `src/`
use the same responsibility folders. All types remain in `idhan::http`.

| Folder        | Responsibility                                                                     |
|---------------|------------------------------------------------------------------------------------|
| `server/`     | Server lifecycle, configuration, TLS certificates, logging, and compression        |
| `routing/`    | Route definitions, registration, reflected handler dispatch, and middleware        |
| `request/`    | Request context, parameters, validation rules, and upload storage                  |
| `response/`   | Response types, reflected response conversion, files, and byte ranges              |
| `json/`       | JSON parsing, serialization, validated values, and missing fields                  |
| `reflection/` | Shared compile-time annotations, type inspection, and request/JSON codecs          |
| `openapi/`    | API documentation annotations, authentication descriptions, and schema compilation |
| `streaming/`  | Server-sent event streams and subscriptions                                        |

Include headers by their full public path, for example
`#include "idhan/http/routing/ReflectedRoutes.hpp"` for route declarations or
`#include "idhan/http/server/HTTPServer.hpp"` for server setup.

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

The 8 MiB threshold selects storage; it is not a request-size limit. Native JSON
and note handlers also read spilled bodies. They impose no application-level byte
cap, although parsing and storage remain subject to available resources and
library representation limits.

Inspect `context.body()` or move ownership with `std::move(context).takeBody()`.
JSON, multipart, and Content-Encoding interpretation belong to the handler.
File creation and positional writes run on an owned worker executor, with each
write awaited before receiving another chunk. Temporary files are unlinked and
close automatically. Their SHA-256 covers the raw body, not extracted file parts.

## Routing and responses

Register `RouteDefinition` values with `RouteRegistry`, then move the registry
and an `HTTPConfig` into `HTTPServer`. Start the server with `start()`. During
shutdown, stop admission, await `drain()`, then call `stop()`.

Handlers receive an owned `RequestContext` and return `HTTPTask<Response>`.
`HTTPTask` aliases `folly::coro::Task`; await Folly tasks from another Folly task
or supply an executor explicitly. Keep blocking work out of route handlers.

Native server routes use reflected declarations in `IDHANServer/src/httpapi/`.
`ReflectedRoutes.hpp` builds route definitions from annotated handlers, validates
parameters, and converts reflected response values. Declare JSON success payloads
with `JsonResponse<T>` or `JsonResult<T>`; return failures with
`Response::error(status, message)`.

For stored files, obtain an owning `folly::File` through the application storage
layer and pass it with `FileMetadata` to `FileResponse::create()`. The response
implementation handles file metadata and supported byte ranges; file bytes
should remain immutable during transmission. The HTTP server uses Proxygen's
compression filters. SSE support lives in `streaming/SSEBodySource.hpp` and
`streaming/SSEEventHub.hpp`.

## Tests

```sh
cmake --build build/debug --target IDHANHttpTests IDHANParameterTests
./build/debug/bin/IDHANHttpTests
./build/debug/bin/IDHANParameterTests
```

HTTP tests exercise event streams and compression. Parameter tests cover reflected
request validation, JSON codecs, response serialization, and handler dispatch.
