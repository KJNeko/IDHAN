Each parser is an npm workspace with its own `package.json` and `index.ts`.
Declare parser-specific dependencies in that parser's manifest. The downloader's
root package owns the shared TypeScript/esbuild tools and the workspace lockfile.
The SDK stays shared under `packages/sdk`.
URL classes and host overrides can set `requestRate: {requests: 8, seconds: 1, spread: true}`
to space request starts evenly (125 ms apart in this example). `spread` defaults to
false, which allows the full burst. The first request after idle can start immediately;
idle time does not accumulate extra allowance. A 429 still applies its cooldown and
reduces the request allowance, increasing the spacing when spread mode is enabled.
Without a valid `Retry-After`, the delay doubles from 1s to 60s. A further 429 after
that maximum pause disables the lane until the user enables it in Downloads. Ten
consecutive HTTP 2xx responses restore one request per window, up to the configured
allowance. Responses already in flight during a pause do not advance recovery.
Add `concurrency: 2` inside `requestRate` to allow up to two occupied transfers
at once; the default is one. This limit is separate from the number of starts
allowed per time window and includes file import finalization. An explicit
`lane.concurrency` takes precedence for existing host policies.
A host override can set `pointsTo: "img4.gelbooru.com"` so stored and compared URLs on
exactly that host use the target host instead, e.g. `img2.gelbooru.com` URLs are normalized
to `img4.gelbooru.com`. Only the host changes; requests still go to the original URL.
Within each lane, queued file imports run before ordinary API/page requests.
Submission order is preserved within each priority. Imports still consume the
same rate allowance and in-flight slots, including retries and redirect hops.

Its release is defined in `packages/sdk/version.ts`; bump it for SDK releases.
Each parser bundle embeds that release in its `sdkVersion` metadata, independently
of the parser's own `version`. IDHAN requires the configured SDK major version
and at least its configured minor and patch version. Older parser bundles without
this metadata must be rebuilt. Policy-only modules do not require it.

From `IDHANDownloader/`:

```sh
npm ci
npm install some-library --workspace @idhan/parser-gelbooru
npm run typecheck:parsers
npm run build:parsers
npm run analyze:parsers
npm run watch:parsers
npm run test:build
```

The `idhanParser` field selects a package's TypeScript entry. esbuild embeds its
SDK imports and npm dependencies in one ES2022 ESM bundle, such as
`dist/gelbooru.js`. The runtime needs neither npm nor `node_modules`. Dependencies
must work without Node APIs because the bundles run in QuickJS.

Add new parser workspace paths to the root `workspaces` list and update the
lockfile with npm. CMake installs manifests and the lockfile into its build
directory, bundles the parsers, and stages their existing runtime filenames.

Bundles are minified and tree-shaken, preserving entry-point exports and code
with side effects. Every build writes `dist/.idhan-metafile.json` and
`dist/.idhan-analysis.txt` with per-input byte contributions. Use
`npm run analyze:parsers` to also print the report, including import chains.
These reports are build artifacts and are not staged into the runtime parsers.

Each bundle also has a linked `.js.map` file containing its original TypeScript
and dependency sources. CMake stages these maps beside the JavaScript bundles,
including prebuilt bundles. Build/watch mode updates and removes maps together
with their bundles. Use source-map-aware tooling to resolve bundle line/column
locations; QuickJS log stack traces still report the generated locations.

Parser execution emits a debug log when QuickJS returns control, showing only
`run` and cumulative `total` execution time per parser within the session runtime.
Nested calls share one timer. Promise settlement, conversions and getters are
included; asynchronous request waits and synchronous host waits are excluded.
Runtime setup, teardown and explicit garbage collection are not profiled.
Times use a monotonic clock with nanosecond storage and the largest applicable
unit (`ns`, `us`, `ms`, `s`, `min`, or `h`) with one truncated decimal, so `1750us`
becomes `1.7ms`. Actual clock resolution depends on the platform.

The QuickJS host provides an OpenSSL-backed WebCrypto subset to every parser:

- `crypto.getRandomValues()` for integer typed arrays (up to 65,536 bytes), and
  `crypto.randomUUID()`.
- `crypto.subtle.digest()` with SHA-1, SHA-256, SHA-384, and SHA-512.
- AES-CTR, AES-CBC (PKCS#7 padding), and AES-GCM via `subtle.encrypt()` and
  `subtle.decrypt()`, with 128-, 192-, or 256-bit keys.
- Raw AES `importKey()`/`exportKey()` and AES `generateKey()`. Key usage and
  extractability restrictions are enforced; imported buffers are copied.

Subtle operations return promises; the native computation currently runs on the
session's JavaScript worker. Unsupported algorithms, key formats, signing,
derivation, and wrapping reject with `NotSupportedError`. This is a supported
subset, not a complete WebCrypto implementation. MEGA file transfers use native
AES-CTR and AES-CBC for decryption and MAC verification; metadata handling remains
in MEGAJS. Run `npm run test:mega-crypto` for interoperability tests against MEGAJS.

Parsers open an IDHAN-managed temporary file with
`const writer = await session.importDirect(metadata)`. Metadata matches `import`
without `request`; optional `url` defaults to the parser invocation URL and is
provenance, never a direct download URL. Call `await writer.write(bytes)` to append
an `ArrayBuffer` or `Uint8Array`, either one chunk at a time or the entire file.
Writes run in call order and wait for storage. Keep each buffer unchanged until
its write resolves. `finish()` finalizes the normal record import; `abort()`
discards the temporary file. Invocation teardown also discards unfinished files.

```ts
const writer = await session.importDirect({
	filename: "image.png",
	urls: [{url: input.url!, type: "post"}],
	discoveredUrls: [], tags: {}, metadata: {}
});
try {
	for await (const chunk of decryptedChunks) await writer.write(chunk);
	// The producer must complete its integrity checks before finishing the import.
	await writer.finish();
} catch (error) {
	await writer.abort();
	throw error;
}
```

The `mega` parser uses pinned MEGAJS 1.3.10 to import public shared files and
folders (including links selecting a file inside a folder). Share URLs must
include their decryption key. Files are decrypted and integrity-checked by
MEGAJS, streamed through `importDirect`, and imported with their original names.
Folder traversal is sequential; account login and uploads are not supported.
