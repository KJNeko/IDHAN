# Docker image

Images are available at `git.futuregadgetlabs.net/kj16609/idhan`:

- `latest`: built from the `master` branch
- `dev`: bleeding edge, built from the `dev` branch
- `vX.Y.0`: built from each matching release tag

# Performance note: seccomp and io_uring

IDHAN uses io_uring for file I/O. Docker's default seccomp profile blocks the `io_uring_setup` and related syscalls, which prevents io_uring from working and causes a significant performance degradation.

The example compose below uses `seccomp=unconfined` to avoid this. This disables Docker's syscall filter entirely, which is a security trade-off — only do this if you understand the implications. A safer alternative is to write a custom seccomp profile that allows only the io_uring syscalls (`io_uring_setup`, `io_uring_enter`, `io_uring_register`) rather than disabling the filter completely.

Running without `seccomp=unconfined` (or an io_uring-permissive profile) is supported but will fall back to standard I/O and may noticeably reduce throughput on large collections.

# Performance note: Postgres shared memory

PostgreSQL runs parallel query plans by putting the shared state — hash tables, tuple queues, parallel scan descriptors — in POSIX shared memory, which means `/dev/shm`. Docker gives a container 64MB of `/dev/shm` by default. That is smaller than a single parallel hash join can ask for, so workers fail part-way through a query with:

```
ERROR: could not resize shared memory segment "/PostgreSQL.876470356" to 16777216 bytes: No space left on device
CONTEXT: parallel worker
```

Nothing warns about this in advance, and it hits exactly the large tag searches that benefit most from parallelism.

Set `shm_size` on the Postgres service. As a rough guide it wants to be at least `work_mem * max_parallel_workers`, with headroom for concurrent queries; `docker-compose-dev.yml` uses `1gb` against its tuned `work_mem=64MB` and `max_parallel_workers=6`. This is unrelated to `shared_buffers`, which is allocated as an anonymous mapping and does not consume `/dev/shm`.

Note that `shm_size` is applied when the container is created. Changing it requires recreating the container (`docker compose up -d --force-recreate idhan_postgres`), not just restarting it. To confirm it took effect, `docker exec idhan-postgres df -h /dev/shm`.

If you cannot change the container configuration, setting `min_dynamic_shared_memory` in `postgresql.conf` preallocates parallel-query memory inside the main shared memory segment instead, bypassing `/dev/shm` — at the cost of reserving that memory unconditionally at startup.

# Important notes

All IDHAN config options can be set via environment variables. ENV variables take priority over the config file but are lower priority than CLI flags. The format is `IDHAN_<GROUP>_<NAME>`, for example:

```toml
[server]
io_threads = 4
```

is equivalent to `IDHAN_SERVER_IO_THREADS=4`.

# Example docker-compose

```yaml
services:
  idhan_postgres:
    image: postgres:18
    container_name: idhan-postgres
    # See "Performance note: Postgres shared memory" above.
    shm_size: 1gb
    environment:
      POSTGRES_USER: idhan
      POSTGRES_PASSWORD: idhan
      POSTGRES_DB: idhan-db
    volumes:
      - idhan_pg18:/var/lib/postgresql
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U idhan -d idhan-db"]
      interval: 10s
      timeout: 5s
      retries: 5
    restart: unless-stopped

  idhan_server:
    image: git.futuregadgetlabs.net/kj16609/idhan:latest
    container_name: idhan-server
    environment:
      IDHAN_DATABASE_HOST: idhan-postgres
      IDHAN_DATABASE_USER: idhan
      IDHAN_DATABASE_PASSWORD: idhan
      IDHAN_DATABASE_DATABASE: idhan-db
      IDHAN_LOGGING_LEVEL: info
    ports:
      - "16609:16609"
    volumes:
      - /path/to/your/media:/files
    security_opt:
      - seccomp=unconfined
    depends_on:
      idhan_postgres:
        condition: service_healthy
    restart: unless-stopped

volumes:
  idhan_pg18:
```
