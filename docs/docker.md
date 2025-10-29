# Docker image

The docker image can be found at `git.futuregadgetlabs.net/kj16609/idhan::latest`

This latest image will be built with each tag. If you want a bleeding edge image, you can use the `dev` tag

# Important notes

All of IDHAN's configs can be set via ENV variables. It will be a higher priority then the config file, but lower then
CLI variables
The format is `IDHAN_{GROUP}_{NAME}` so a config that would fit under:

```toml
[server]
port = 8080
```

would be `IDHAN_SERVER_PORT=8080`

# Example docker-compose
You should likely be competent enough to understand how to use this

```
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
    image: git.futuregadgetlabs.net/kj16609/idhan:dev
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
      - /mnt/bucket-of-bits/Media/IDHAN:/files
    security_opt:
      - seccomp=unconfined
    depends_on:
      idhan_postgres:
        condition: service_healthy
    restart: unless-stopped

volumes:
  idhan_pg18:
```