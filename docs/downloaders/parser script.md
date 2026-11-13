Parsers are TypeScript modules with a default-exported class decorated with `@parser`.
Compile them into JavaScript bundles before starting IDHAN. IDHAN validates each bundle
and extracts its metadata before accepting API requests.
Preparation errors are reported without aborting server startup. Updating a parser
publishes its routes and checked modules together; failed edits retain the working version.

```typescript
import {applyOptions, generator, option, parser} from "./packages/sdk/decorators.js";
import {bindParser} from "./packages/sdk/runtime.js";
import type {GeneratorInput, ParserMetadata} from "./packages/sdk/parser.js";
import type {DomainContext, Session} from "./packages/sdk/session.js";

const parameters = {
    id: {name: "Post ID", type: "string"}
} as const;

@parser({id: "example", version: 1})
export default class Example {
    declare static readonly metadata: ParserMetadata;

    @option({type: "boolean", default: false, description: "Follow source URLs."})
    accessor followSources = false;

    constructor(private readonly site: DomainContext) {
        applyOptions(this, site.options);
    }

    @generator("post", parameters)
    async post(input: GeneratorInput<{ id: string }>, session: Session): Promise<void> {
        // Fetch and validate the response, then queue files with session.queueImport(...).
    }
}

export const run = bindParser(Example);
```

Each parser has one global QuickJS runtime with its own dedicated thread by default.
All download sessions using that parser share it. `downloader.parser_instances` sets
the maximum number of instances per parser, allocated lazily; each additional instance
owns another thread. It defaults to `1` and replaces `downloader.script_threads`.
Instances survive individual sessions and are destroyed on their owning threads at
downloader shutdown. Each instance blocks on a counting semaphore for its own event queue while idle.
Enqueueing releases one permit; the owning thread takes and processes one event per permit. New
invocations, completed native operations, and session capacity changes notify only
relevant instances. Ready JavaScript microtasks run in bounded batches; awaiting an
external operation never keeps the thread polling. HTTP and import concurrency
remain separately bounded.

Within each instance, the SDK retains one parser object per parser, origin (scheme,
hostname, and port), published registry version, and secrets snapshot. Saving changed
secrets selects a fresh object for subsequent reads so authentication setup runs again;
already running calls retain their original snapshot. The constructor receives domain
options and secrets; `configure()` runs once before generator calls. Domain setup can
install cookies and HTTP Basic authentication. A parser update creates fresh objects
for subsequent invocations, while existing calls keep their original objects.

Lane request rates cap starts across every shard using a sliding window: a rate of 10 requests per
second can start all 10 at once, and each slot reopens one second after its own request started.
They do not serialize whole transfers. HTTP 429 responses reduce the current allowance
by one request per window, to a minimum of one, while retaining the configured rate
as the recovery ceiling. Valid `Retry-After` headers set the pause. Without a valid
header, successive retries pause for 1, 2, 4, 8, 16, 32, then 60 seconds. Another 429
after the 60-second fallback pause disables the lane: queued and new requests fail
until the user clicks **Enable lane** in Downloads (or explicitly resets its backoff
through `POST /rate_limits/reset?lane=...`). Disabled state survives lane retirement
and host-setting refreshes. Responses already in flight during a pause share it and
do not advance the fallback sequence. Ten consecutive HTTP 2xx responses after the
pause restore one request per window and restart the fallback delay at one second;
recovery stops at the configured allowance. Failed responses interrupt the success
streak. Parser errors and configured non-429 backoff statuses retain their separate
`lane_error_backoff_seconds` cooldown. Bandwidth is monitored without a speed cap.
The default occupied-slot limit is 32 for all lanes, overridden
by the URL class/host's concurrency or the downloader's lane-concurrency settings.
The in-flight counter includes network requests and imports finishing after download; both
consume bounded slots. Queue state identifies rate cooldowns, backoff, and occupied-slot
limits. Dispatch and completion changes are streamed immediately rather than waiting
for the periodic connection-pool sweep.

Generator and discovery methods receive an invocation-specific `Session` as their
second argument. Use that argument for requests, follows, and imports, and keep it local
to the call: methods on the same parser object can overlap across sessions and awaits. Store only
domain configuration and deliberately shared state on the parser object. Construction
and configuration do not receive invocation-bound request, follow, or import methods.
Use `session.fetch(...)` for Fetch API responses and `session.delay(milliseconds)` for
timers. These retain the calling invocation's host, unlike globals captured by a
parser method from an earlier invocation. Direct import writers accept byte buffers
from other realms in the same runtime.

Parsers can cache JSON values through `site.kv` (also available as `session.kv`).
The in-memory store is shared across sessions, parser instances, and parser reloads,
scoped by parser ID, including across different site origins. It is cleared when the downloader shuts down.
`get(key)` returns `undefined` for missing or expired entries; successful reads restart
that entry's lifetime. `set(key, value, lifetimeSeconds?)` stores a JSON copy and defaults to
one day (86,400 seconds). A custom lifetime must be a positive finite whole number
of seconds (at least 1); it is retained for subsequent reads. `delete(key)` removes
an entry and returns whether it was present. Retrieved objects are independent copies;
call `set` to save edits. The authenticated `GET /downloader/cache` endpoint returns
hit/miss totals since downloader startup, a hit-rate fraction (0–1), the live key count,
and the top 100 live keys ordered by descending hits, then parser ID and key. Each row
includes its parser, key, hit count, lifetime, and remaining expiry in seconds; cached
values are omitted. Polling does not record hits or refresh expiry. Totals retain reads
of expired/deleted entries; per-key hits last for that entry's lifetime and survive
replacement of an unexpired value. The Downloads panel polls this endpoint every two
seconds. Expired entries are removed on access, swept during store activity at most
once per minute, and removed whenever statistics are polled. Gelbooru uses this store for tag-type lookups.

```ts
session.kv.set("tag-type:artist_name", 1); // One day, refreshed on each read.
const type = session.kv.get<number>("tag-type:artist_name");
session.kv.set("short-lived", {value: 42}, 60);
session.kv.delete("short-lived");
```

Request/import completion logs report the outgoing HTTP headers' send timestamp in
UTC and the transfer duration from that send to response completion. Preparation and
waiting before send are shown separately, so lane cooldowns and queue delays do not
appear as network transfer time. Retries and redirects report their current attempt;
a connection failure before headers are sent is reported as `not sent`.

`worker_memory_limit` applies to each parser runtime. Session diagnostics report the
activity of the parser instances used by that session; shared instance activity can
therefore appear in more than one session's snapshot.

`session.queueImport(...)` expects the parser to supply a direct file URL, including when
using POST or a request body. It submits work immediately and returns an import
handle without creating a promise. Read `handle.importFinished` to create a promise that
waits for the file and its metadata/session-row persistence and returns `{existing}`.
Repeated reads reuse that promise. Status can be requested before or after completion
within the same download session. Preparation, download, or persistence failures
reject the status promise and are recorded by the session even if status is never requested.

```ts
await session.queueImport(file).importFinished; // Continue after this import completes.
session.queueImport(otherFile); // Continue immediately; the session owns the import.
```

Ignoring the handle retains no JavaScript references in the queued operation.
When status is requested, promise resolution and handlers run on the creating script
worker. Its realm stays alive for pending status promises so handlers can run even after
the parser function returns. Realm teardown discards promise handles on their owner
thread while native completion and cleanup remain owned by the session. A session
remains busy until its queued imports finish. Import
preparation (URL declarations, session rows, and reuse checks) runs on import workers.
Submissions are staged in session memory without waiting for a worker slot. Each
session limits preparation tasks submitted to the executor using
`session_inflight_requests`; excess submissions remain staged until capacity opens.
Staged and active preparations count toward the capacity for starting new parser work.
Disk writes and incremental SHA256 hashing run on writer workers with a 1 MiB
buffer per transfer; curl pauses a full buffer until it drains below 512 KiB.
Completed downloads move off the network event loop for storage, metadata, and
database work. Import-row inserts and completions are batched across workers with
up to a 1 ms collection window, and URL declarations use a bulk query per import.
`downloader.import_threads` sets the worker count in each of the preparation,
writer, and finalization pools (default 4). Shutdown drains all stages before
releasing the session's host adapters. Diagnostics snapshots are assembled when
requested, rather than copied and sorted on every queue transition.

Script workers never wait for database-backed host callbacks. Lifecycle observers,
follow lookups, URL-type persistence, and discovery persistence run on an ordered
host-work queue per session. Followed rows are saved before child work is published.
These callbacks, import reuse, and file finalization await database coroutines, so
pending queries release their host/import executor threads. Batched import rows use
coroutine batons and an asynchronous collection timer; waiting for a batch never
blocks a worker. Response cookies are persisted through coroutine hooks and a
coroutine writer mutex, keeping redirects ordered without blocking network or import
workers. Disk-buffer draining also suspends the finalizer instead of parking a thread.
The transport tracks suspended completions in an async scope and joins it before
stopping its executors. Synchronous adapter hooks remain for lightweight hosts;
database-backed adapters must override the coroutine hooks.
`session.follow(...)` checks the destination against registered parser routes,
including learned hosts. Matching pages use their destination parser; unclaimed
HTTP(S) URLs download directly, with follow tags applied to the imported file.
Known page URLs without a handler remain skipped. Discovery hooks remain available
for submitted root URLs.

`session.follow(...)` awaits host completion through a promise while its JavaScript
worker can advance other invocations. The native `idhan.setUrlType(...)` and
`idhan.learnHost(...)` functions also return promises; await them when subsequent
work depends on their persistence. Discovery awaits its host mapping before invoking
the discovered parser. Native import persistence runs independently of its originating
realm, including during shutdown. Session idle/wait accounting includes queued host callbacks,
and shutdown drains them before releasing observers. Secrets are read from cached
snapshots; secret-provider implementations must not perform I/O on a script worker.
Runtime module loading also uses only prepared bytecode and saved module resolutions.
An import outside that snapshot fails immediately; it never falls back to reading or
compiling a file on a script worker. Publish modules through parser startup/reload
preparation before invoking them, including dynamic import targets.

Native URL-class patterns are validated when registry snapshots are prepared, then
compiled matchers are reused per thread. Route results are bounded and cached within
a registry snapshot; changing a saved host invalidates those results.
Lane settings use the same snapshot and host-generation invalidation. Script workers
pump pending microtasks before sleeping and wake on work or capacity changes. Direct
file URLs skip JavaScript realm creation; full GC runs every 64 released realms,
alongside QuickJS's automatic memory-pressure collection.

Gelbooru searches fetch each batch at `pid=0` in descending post-ID order. After a
full batch, the next request adds `id:<oldest-returned-ID` to the original filters.
This avoids the API's deep-offset limit and excludes the preceding boundary post.
Search sort terms are replaced with `sort:id:desc` so the ID boundary cannot skip
matching posts. The parser rejects non-decreasing IDs instead of looping when a site
ignores the ordering or boundary. `pageLimit` counts total batches across the search;
zero remains unlimited. Post page size and tag lookup batch size remain independent.

`@option` declares each option's `type` (`boolean`, `number`, `string` or `string[]`), its
`default` and a description. TypeScript requires the accessor's type to match the declared
`type`, and the initializer is validated at runtime. Numeric constraints such as `minimum` and
`integer` belong in `@option`; overrides are validated at runtime.

`@parser` collects `@generator`, `@discovery` and `@option` declarations without constructing
the parser or a Session. Optional static cookies belong in `@parser({ cookies: [...] })`.
An optional instance `configure()` method can set cookies or authentication at runtime.
Discovery methods use `@discovery` to identify otherwise unrecognized sites; they are not generators.
Successful discovery persists an exact host-to-parser mapping in `downloader_parser_hosts`,
with `auto_discovered = true`. The server restores these mappings on startup and uses the
parser's URL classes directly for subsequent URLs. Parser reloads retain the mappings.
The Downloads options list each parser's saved hosts with their provenance and allow users
to add or remove them. User additions have `auto_discovered = false`; discovery preserves
existing mappings. Removing a mapping allows the host to be discovered again later.

`session.secrets` contains the JSON object for the explicitly configured root domain.
`example.co.uk` shares its object with `subdomain.example.co.uk`, while lookalike and unrelated
domains remain isolated. Missing configuration produces `{}`. The parser validates the fields
it requires. Secrets are loaded and decoded once at server startup; parser reads use
an in-memory snapshot. Successful API updates publish their new values immediately,
without querying the database again. A complete downloader reload refreshes the snapshot;
failed writes or reloads retain the last successfully published values. External database
edits become visible at startup or a complete reload. The same parser can serve multiple domains with separate
credentials.

Bundled URL classes are typed constants supplied to `@parser({ urlClasses })`. Independent
JSON URL classes may reference the parser ID and its generator IDs without changing the parser.
They use `id`, `version`, `hosts`, `parser`, `url_patterns` and `generators`; matching uses native
`URLPattern` with order-independent query requirements. The bundled JSON files show the format
and include host-specific request-rate policies.

The C++ host embeds QuickJS and accepts only `.js` and `.mjs` modules. It has no
TypeScript compiler, compiler subprocess, or runtime Node.js dependency. TypeScript
and esbuild are development/build dependencies only. Standard decorator lowering
preserves the SDK's option and generator metadata.

Build the bundled parsers outside IDHAN:

```sh
npm ci --prefix IDHANDownloader
npm --prefix IDHANDownloader run build:parsers
```

The build type-checks all parser sources first, then produces one self-contained ES2022
ES module per parser in `IDHANDownloader/dist/`. Shared SDK code is included in each
bundle. The discovery dispatcher has its own bundle at `packages/sdk/discovery.js`;
its imports of parser bundles remain dynamic. CMake runs this same build step and
stages the bundles and non-code assets under `<build-dir>/bin/downloaders/parsers`.
Docker builds the bundles in a separate Node `parserbuilder` stage. The C++ builder
uses `IDHAN_DOWNLOADER_PREBUILT_DIR` to consume them without Node/npm, and the runtime
image ships only JavaScript assets. Local CMake builds may use the same prebuilt
option or leave it unset to build bundles automatically.

Both parser test targets build their bundle dependencies automatically:

```sh
cmake --build build/debug --target IDHANDownloaderScriptTester
cmake --build build/debug --target IDHANDownloaderTests
```

Premade parser and SDK edits, additions, and deletions invalidate the bundle build.
Missing generated bundles are rebuilt, and the C++ tests also rebuild their authored
TypeScript fixtures when fixture sources or build dependencies change. Leave
`IDHAN_DOWNLOADER_PREBUILT_DIR` unset to compile the current checkout's sources.

For development, run the external watcher in a separate terminal:

```sh
npm --prefix IDHANDownloader run watch:parsers
```

Set `downloader.parser_directory` (or `IDHAN_DOWNLOADER_PARSER_DIRECTORY`) to the
absolute path of `IDHANDownloader/dist/`. Alternatively, write directly to an existing
build's parser directory:

```sh
npm --prefix IDHANDownloader run watch:parsers -- --outdir "$PWD/build/debug/bin/downloaders/parsers"
```

The external watcher type-checks changes and rebuilds affected output. A type or
bundling error leaves the previous JavaScript intact. Its output manifest removes
obsolete generated bundles after source deletion, while preserving independently
supplied files. Files are published by atomic rename; the output directory must be
separate from the source directory. Additional JSON assets and secrets are supplied
separately; they are not bundled into JavaScript.

IDHAN watches JavaScript changes, additions, deletions, and configured URL-class JSON.
Polling compares file sizes and modification times, without rereading source contents.
It reloads all parser entries after a quiet period, including when a shared JavaScript
module changes. Existing invocations retain their bytecode; new route resolutions use
the newly published registry. Broken JavaScript retains the last working parser.
The watch settings are `downloader.watch_scripts` (default `true`),
`script_watch_interval_ms` (default `500`), and `script_watch_debounce_ms` (default `250`).
Reloading a set of files is debounced, but publication of multiple independent bundle
files is not an atomic deployment of the entire directory.

`npm --prefix IDHANDownloader run typecheck:parsers` checks sources without building.
`npm --prefix IDHANDownloader run test:build` checks bundling, output cleanup, error
recovery, and the external watcher. Runtime C++ tests use fixtures bundled at build time.

The script tester accepts per-parser overrides with
`--options '{"example":{"followSources":true}}'` and a domain-to-object secret file with
`--secrets path/to/secrets.json`.
