# Config file location

- IDHAN looks for a config file next to its executable first (`./config.toml`)
- If one cannot be found there it looks for one at `~/.config/idhan/config.toml`
- You can also specify the config path with `--config <PATH>` — this replaces the file search entirely (see launch options)
- IDHAN will only create a default config file if it cannot find one

# Launch options

- `-h` `--help`: Print help
- `-v` `--version`: Print the version and exit
- `--use_stdout [on/off]`: Enable logging to stdout (enabled by default; pass `--use_stdout 0` to disable)
- `--log_level <LEVEL>`: Set the log level (`trace`, `debug`, `info`, `warning`, `error`, `critical`)
- `--config <PATH>`: Use this config file exclusively. IDHAN will not load configs from other locations.
- `--pg_user <USER>`: PostgreSQL user (overrides the config file)
- `--pg_host <HOST>`: PostgreSQL hostname (overrides the config file)
- `--pg_schema <SCHEMA>`: PostgreSQL schema to use (overrides the config file, default `public`)
- `--force_start [on/off]`: Force IDHAN to start even if it detects a previous instance may still be running

Every option also accepts `--name=value`. The two flag-style options (`--use_stdout`, `--force_start`) may be
written bare, in which case they read as on.

# Config order

IDHAN searches for each config value in the following order (highest priority first).

All config options can also be set via environment variables using the format `IDHAN_<GROUP>_<NAME>` (e.g. `IDHAN_DATABASE_HOST=localhost`). ENV variables take priority over config files. If `--config <PATH>` is passed, only that file is consulted — the paths below are skipped.

## Linux

1. CLI flags (`--pg_user`, `--pg_host`, `--pg_schema`, `--log_level`, `--use_stdout`)
2. Environment variables (`IDHAN_<GROUP>_<NAME>`)
3. `./config.toml`
4. `~/.config/idhan/config.toml`
5. `/etc/idhan/config.toml`
6. `/usr/share/idhan/config.toml`

## Windows

1. CLI flags
2. Environment variables
3. `./config.toml`
4. `%LOCALAPPDATA%\idhan\config.toml`
5. `%APPDATA%\idhan\config.toml`
6. `%ProgramData%\idhan\config.toml`

# Config options (`config.toml`)

The following example covers the most common options. Commented-out lines show the default values.

```toml
[database]
#host = "localhost"
#port = 5432
#user = "idhan"
#password = "idhan"
#database = "idhan-db"
# Schema put on the connection's search path
#schema = "public"

[server]
# TCP port the HTTP server listens on
#port = 16609
# HTTP event loop threads (0 = every thread this machine has)
#io_threads = 4
# Background job runtime threads (default: a quarter of this machine's, minimum 2; 0 = every thread)
#job_threads = 4
# Temporary file path for uploads
#temp_path = "/tmp/idhan"

[host]
# Listen address for IPv4 (set to "" to disable)
#ipv4_listen = "127.0.0.1"
# Listen address for IPv6 (set to "" to disable)
#ipv6_listen = "::1"
# Enable TLS
#use_tls = false
# Paths to TLS certificate and key (required when use_tls = true)
#server_cert_path = "./server.crt"
#server_key_path = "./server.key"

[thumbnails]
#path = "./thumbnails"
# Write generated thumbnails to disk at all
#cache = true
# Only these requested sizes are cached; other sizes are generated and served, never written
#cacheable_sizes = [128, 256, 512]

[modules]
# Calls one module worker process runs at once
#pool_threads = 4
# Threads one call may use inside a module (libvips, video decoders)
# Unset, pool_threads * render_threads comes to 25% of this machine's threads
#render_threads = 2

[logging]
# Logging level: trace, debug, info, warn, error, critical
#level = "info"
# Log output directory
#path = "./log"
# Entries held in memory for the /log endpoint; 0 disables the endpoint
#buffer_size = 0
# Warn when a request exceeds the performance threshold
#enable_perf_warnings = false

[parser.cbz]
# Fall back to the .cbz extension when the archive's contents are inconclusive
#guess_with_extension = true

[cluster]
# Concurrent files per cluster scan; per-request "concurrency" overrides it, clamped to 1..64
#scan_concurrency = 4
```

## Per-library module settings

Every key under `[modules]` can be overridden for a single module library by putting it in a
`[modules.<library>]` table, where `<library>` is the module file's stem. This is how a thumbnailer
is given its own thread budget: `[modules.IDHANVips]` sizes the image thumbnailer independently of
`[modules.IDHANFFmpeg]`, which does the video ones.

```toml
[modules]
pool_threads = 4

# The image thumbnailer: many cheap calls, few threads each.
[modules.IDHANVips]
pool_threads = 8
render_threads = 4

# The video thumbnailer: few expensive calls, many threads each.
[modules.IDHANFFmpeg]
pool_threads = 2
render_threads = 8
```

Overridable keys: `pool_threads`, `render_threads`, `rss_limit_mb`, `idle_timeout_sec`,
`heartbeat_interval_ms`, `liveness_grace_ms`. Anything not named falls back to the `[modules]` value,
then to the built-in default. The library names shipped with IDHAN are `IDHANVips`, `IDHANFFmpeg`,
`IDHANPsd`, `IDHANArchive`, `IDHANUgoira` and `IDHANEmbedding`; the server logs the resolved numbers
for each one at startup.

The two knobs mean different things:

- `pool_threads` is how many calls that library's worker process runs at once. Raising it raises
  throughput and the worker's peak memory in equal measure.
- `render_threads` is how many threads one call may use *inside* the module, and is what bounds
  libvips' per-operation pool and the video decoders' thread count.

A worker's thread budget is `pool_threads * render_threads`. The server itself is deliberately
unrationed, but module workers are: left unset, that product resolves to **25% of the machine's
threads**, because libvips and the codec pools will each otherwise spin up one pool per core per
call. Setting either key explicitly overrides the budget. **Zero on either means every thread the
machine has**, the same convention `[server] io_threads` uses.

Both are also settable per library through the environment, e.g.
`IDHAN_MODULES_IDHANVIPS_POOL_THREADS=8`.

### Single-threaded libraries

A module declares whether one call can use more than the thread it arrived on, through
`ModuleBase::singleThreaded()`. It returns **true by default**: a module that parallelises inside a
call opts out of it, never into it.

When *every* module in a library is single-threaded, there is nothing inside a call for the render
budget to be spent on, so the worker spends it on concurrency instead: `pool_threads` is multiplied
by `render_threads`, and `render_threads` drops to 1. Two render threads and eight pool threads
become sixteen concurrent calls. The thread count is identical either way; only the split changes.
The rule is all-or-nothing per library because the modules share one worker process, so one module
that does parallelise would otherwise get both the multiplied pool and its own render threads.

Of the shipped libraries `IDHANPsd` and `IDHANEmbedding` (which pins ONNX to one intra-op thread) are
single-threaded; the libvips, FFmpeg, Ugoira and archive thumbnailers are not, which keeps their
libraries unfolded. The server logs which way each library resolved at startup.

Concurrency, unlike threads, costs memory: doubling a worker's concurrent calls doubles its peak
resident set. `rss_limit_mb` is per-library too if a folded worker needs a different ceiling.

> **Note:** The server listens on port **16609** by default. Override it with `[server] port` (or
> `IDHAN_SERVER_PORT`). Both the IPv4 and IPv6 listeners use this port.
