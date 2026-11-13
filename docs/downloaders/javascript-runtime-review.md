# JavaScript bundle runtime

The downloader keeps TypeScript for authoring and runs precompiled JavaScript in
QuickJS. See [parser authoring](parser%20script.md) for build and watch commands.
The WebUI continues to use its separate Vite pipeline.

## Build boundary

`IDHANDownloader/scripts/build-parsers.mjs` uses the pinned TypeScript compiler
for type checking and esbuild for standalone ES2022 module bundles. Parser entry
points embed their shared SDK dependencies. The discovery dispatcher is bundled
separately and dynamically selects parser bundles at runtime.

`build:parsers` performs a finite build; `watch:parsers` runs the TypeScript file
watcher outside IDHAN and rebuilds after successful type checking. No files are
published on type-check or bundling failure. Writes use atomic file replacement,
and a manifest removes only output previously owned by this build.

Docker precompiles bundles in its own Node `parserbuilder` stage. CMake consumes
those bundles through `IDHAN_DOWNLOADER_PREBUILT_DIR` and stages JavaScript plus
non-code assets without requiring Node/npm in the C++ builder. Local CMake builds
continue to compile bundles when the prebuilt option is unset.
Tests likewise precompile their authored TypeScript fixtures. The final Docker
image includes neither the runtime compiler directory nor Node.js.

The previous `TypeScriptCompiler` C++ class, its subprocess wrapper, compiler
staging target, and TypeScript fallback resolution have been removed. Runtime
module extensions are restricted to `.js` and `.mjs`. The bytecode cache still
compiles JavaScript into QuickJS bytecode as part of JavaScript execution.

## Reload boundary

`ScriptWatcher`, owned by `DownloaderContext`, polls JavaScript contents and
configured URL-class JSON, batches changes after a quiet period, and calls
`ScriptRegistry::updateParsers()`. It includes shared `packages/` modules in its
scan. Watch failures retain the previous snapshot and are logged without repeating
identical scan errors. Shutdown stops and joins the watcher before destroying
workers or the registry. Watching can be disabled through downloader configuration.

The existing registry prepares metadata and bytecode before publishing routes.
An invocation holds its own cache, so an old invocation can finish after a new
version is published. Invalid edits preserve the previously prepared parser.
Reload failures are available through the existing parser-status API.

Each published bundle is complete, but a directory of multiple bundles is not one
atomic deployment. Debouncing batches normal build bursts; callers requiring an
atomic deployment across unrelated parsers need an external deployment protocol.

## Validation coverage

Build tests cover embedded dependencies, failed type checks, successful recovery,
manifest cleanup, and rebuilds/deletions in external watch mode. C++ tests cover
bundled parser contracts and metadata, rejection of TypeScript at runtime,
JavaScript replacement and error recovery, additions/deletions, shared module
changes, URL-class JSON changes, and preservation of old route bytecode.
