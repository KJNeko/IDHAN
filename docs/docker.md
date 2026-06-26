# Docker image

Images are available at `git.futuregadgetlabs.net/kj16609/idhan`:
- `latest` — built from each release tag
- `dev` — bleeding edge, built from the `master` branch

# Performance note: seccomp and io_uring

IDHAN uses io_uring for file I/O. Docker's default seccomp profile blocks the `io_uring_setup` and related syscalls, which prevents io_uring from working and causes a significant performance degradation.

The example compose below uses `seccomp=unconfined` to avoid this. This disables Docker's syscall filter entirely, which is a security trade-off — only do this if you understand the implications. A safer alternative is to write a custom seccomp profile that allows only the io_uring syscalls (`io_uring_setup`, `io_uring_enter`, `io_uring_register`) rather than disabling the filter completely.

Running without `seccomp=unconfined` (or an io_uring-permissive profile) is supported but will fall back to standard I/O and may noticeably reduce throughput on large collections.

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
