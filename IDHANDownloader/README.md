# Downloader code map

Public C++ interfaces live in `include/IDHANDownloader/`. Implementation files
live in `src/`. Start with `DownloaderContext.hpp` to create the downloader,
`DownloaderConfig.hpp` for settings, and `DownloaderHost.hpp` for the services
provided by the embedding application (the server or standalone tester).

| System             | Public headers | Implementation | Responsibility                                                               |
|--------------------|----------------|----------------|------------------------------------------------------------------------------|
| Sessions           | `session/`     | `session/`     | Crawl lifetime, queued work, progress, events, and parser scheduling         |
| Imports            | `imports/`     | `imports/`     | Hand files and their metadata to the embedding application's import backend  |
| HTTP               | `http/`        | `http/`        | Transfers, host lanes, rate limits, bandwidth, and request timing            |
| Cookies            | `cookies/`     | `cookies/`     | Cookie storage, request cookies, and persistence hooks                       |
| Parsers            | `parsers/`     | `scripts/`     | Parser discovery, registration, URL classes, updates, and bytecode caching   |
| Secrets            | `secrets/`     | `secrets/`     | Credential lookup and domain scoping                                         |
| URLs               | `urls/`        | `urls/`        | URL comparison and URL utilities                                             |
| JavaScript runtime | —              | `js/`          | Execute parser scripts with QuickJS; expose host APIs through `js/bindings/` |

Parser source code and the TypeScript SDK live in `premade/`. The standalone
command-line host lives in `tester/`. The server's database and file-storage
implementations of the host interfaces live in `IDHANServer/src/downloader/`.

## Import flow

`const handle = session.queueImport(options)` queues a file immediately without
creating a promise or retaining the parser. Ignore the handle for fire-and-forget
imports, or use `await handle.importFinished` to wait for persistence and receive
`{existing}`. The promise is created on the first `importFinished` access; subsequent reads
return the same promise. A handle can report completion even after the import has
finished, while its download session is still available. It is not transferable
to another session.

The handle and queued request share a native completion record without JavaScript
references. The session reports completion and failures through session events even
when no status is requested. Queued imports still consume native queue memory until
processed. The native `idhan` host exposes the same `queueImport()` method.

The structs in `imports/ImportSink.hpp` describe the boundary between the
downloader and its import backend. They are separate from the parser's JavaScript
arguments and HTTP request options.

1. The parser calls `idhan.queueImport(options)` with a direct file URL.
2. Unless forced, the session checks `ImportSinkFactory::findStoredRecord()`
   for a locally stored file matching the direct URL. The check returns an optional
   record ID and does not apply metadata or complete the import.
3. `ImportSinkFactory::open()` creates a sink. The request's `stored_record` holds
   the matching record ID, if any.
4. When no stored record is available, HTTP transfers deliver file chunks through
   `ImportSink::write()`.
5. Both cases call `finish(ImportMetadata)`. The backend uses the stored record
   or imports the downloaded bytes, then applies tags, URLs, and session links in
   the same completion step. Downloads also supply the byte count, content type,
   filename, and final URL after redirects.
6. The backend returns `ImportResult`: the record ID, whether it already existed,
   and a human-readable note. The parser's promise currently resolves without
   exposing that result, or rejects on failure.

Forced imports skip the stored-file check. The parser is responsible for supplying
an import URL that identifies a file, regardless of the HTTP method or request body.
With `importDirect()`, the parser supplies bytes through a writer instead of an HTTP
transfer; finishing the writer still finalizes the backend sink. Its associated
page URLs are not classified as direct file URLs.

## HTTP rate limits

Each host (or configured lane group) has one request-start budget shared by all
sessions and parser instances. API post and tag requests to the same host consume
the same budget. Refreshing host settings preserves recent starts and server
backoff. A 429 pauses that shared lane before the transport releases its slot,
without waiting for cookies or completion workers. Already-started requests may
still finish during the pause.

## Follow routing

`follow()` checks the destination URL against the registered parser routes,
including learned hosts. Matching pages run their destination parser; unclaimed
HTTP(S) URLs go directly to file download and import, even on another domain.
Follow tags reach either path, and repeated URLs are still deduplicated. Known
page URLs without a handler remain skipped. Submitted root URLs can still run
discovery hooks before falling back to file download.

The selected route travels with queued work, so a followed file is not sent back
through discovery when execution begins.
