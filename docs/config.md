# Config file location

- IDHAN looks for a config file next to its executable first (`./config.toml`)
- If one cannot be found there it looks for one at `~/.config/idhan/config.toml`
- You can also specify the config path with `--config <PATH>` — this replaces the file search entirely (see launch options)
- IDHAN will only create a default config file if it cannot find one

# Launch options

- `-h` `--help`: Print help
- `--testmode`: Forces the PostgreSQL schema to `test` instead of `public` (used for automated tests)
- `--use_stdout`: Enable logging to stdout
- `--config <PATH>`: Use this config file exclusively. IDHAN will not load configs from other locations.
- `--pg_user <USER>`: PostgreSQL user (overrides the config file)
- `--pg_host <HOST>`: PostgreSQL hostname (overrides the config file)

# Config order

IDHAN searches for each config value in the following order (highest priority first).

All config options can also be set via environment variables using the format `IDHAN_<GROUP>_<NAME>` (e.g. `IDHAN_DATABASE_HOST=localhost`). ENV variables take priority over config files. If `--config <PATH>` is passed, only that file is consulted — the paths below are skipped.

## Linux

1. CLI flags (`--pg_user`, `--pg_host`)
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

[server]
# Number of IO threads (0 = auto-detect from hardware)
#io_threads = 0
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

[logging]
# Logging level: trace, debug, info, warn, error, critical
#level = "info"
# Log output directory
#path = "./log"
# Ring buffer size for the async logger
#buffer_size = 1000000
# Warn when a request exceeds the performance threshold
#enable_perf_warnings = false
```

> **Note:** The server always listens on port **16609**. This is not configurable via the config file.
