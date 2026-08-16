# Docker image

Images are available at `git.futuregadgetlabs.net/kj16609/idhan`:

- `latest`: built from the `master` branch
- `dev`: bleeding edge, built from the `dev` branch
- `vX.Y.0`: built from each matching release tag

# Performance notes

## secomp and io_uring

Since IDHAN uses io_uring for I/O Docker might sometimes force it to fallback to non-io_uring due to seccomp. For the
best performance you should run with `seccomp=undefined` for the easier setup. A safer approach is just to allow
io_uring and it's related syscalls

## Postgres shared memory (Docker specific)

Postgresql can sometimes get held up by `/dev/shm` being too small. This really only happens when you start to have
large queries. It can work without it being smaller but performance might be hurt as a result. You can raise this value
in docker by seing `shm_size` when creating the postgresql container.

# Semantic search models

The image ships no embedding model. The fp32 siglip2 pair is around 1.5 GB, which is larger than the rest of the image
put together, and every deployment would pay to pull it whether or not it uses semantic search. Models are supplied by
the operator.

`IDHAN_EMBEDDINGS_MODEL_PATH` defaults to `/usr/share/idhan/models`, which exists in the image but is empty. Mount a
model directory under it, one subdirectory per model:

```yaml
    volumes:
      - /path/to/siglip2-base-patch16-224:/usr/share/idhan/models/siglip2-base-patch16-224
```

The subdirectory name is the model name IDHAN registers. The expected layout is the
one [onnx-community](https://huggingface.co/onnx-community/siglip2-base-patch16-224-ONNX) publishes:
`onnx/vision_model.onnx`, optionally `onnx/text_model.onnx` for text queries, plus `preprocessor_config.json` and
`tokenizer.json` beside them. A flat directory with `model.onnx` at the top level also works.

To fetch the fp32 siglip2 pair without cloning the repository, which carries eight precision variants of each tower and
runs to roughly 11.4 GB:

```bash
mkdir -p siglip2-base-patch16-224/onnx && cd siglip2-base-patch16-224
REPO=https://huggingface.co/onnx-community/siglip2-base-patch16-224-ONNX/resolve/main
for file in config.json preprocessor_config.json tokenizer.json tokenizer_config.json special_tokens_map.json; do
    curl -fL -o "$file" "$REPO/$file"
done
for tower in vision_model text_model; do
    curl -fL -o "onnx/$tower.onnx" "$REPO/onnx/$tower.onnx"
done
```

The repository also carries `_fp16`, `_int8`, `_uint8`, `_q4`, `_q4f16` and `_bnb4` variants of each tower, named by
suffix in the same `onnx/` directory. Fetching one of those instead needs no renaming: IDHAN looks for `""`, `_fp16`,
`_q4f16`, `_q4`, `_int8`, `_uint8` and `_quantized` in that order and takes the first present, so a directory holding
only `vision_model_q4f16.onnx` loads that. `_bnb4` is not recognised. fp32 is the default because inference is CPU-only
and onnxruntime's CPU provider has to undo fp16; `_q4f16` cuts the pair to roughly 0.5 GB if size matters more than
fidelity.

An empty or absent model directory is not an error. The server logs that no embedding model is available, semantic
search is unavailable, and everything else runs normally.

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
      # Optional, for semantic search. See "Semantic search models" above.
      # - /path/to/siglip2-base-patch16-224:/usr/share/idhan/models/siglip2-base-patch16-224
    security_opt:
      - seccomp=unconfined
    depends_on:
      idhan_postgres:
        condition: service_healthy
    restart: unless-stopped

volumes:
  idhan_pg18:
```
