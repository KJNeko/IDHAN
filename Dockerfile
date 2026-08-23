# syntax=docker/dockerfile:1.7

# Stage 0: WebUI
FROM --platform=$BUILDPLATFORM node:22-slim AS webbuilder

ENV COREPACK_ENABLE_DOWNLOAD_PROMPT=0
RUN corepack enable
WORKDIR /web

COPY IDHANWeb/package.json IDHANWeb/pnpm-lock.yaml IDHANWeb/pnpm-workspace.yaml ./
RUN --mount=type=cache,target=/pnpm-store \
    pnpm config set store-dir /pnpm-store && pnpm install --frozen-lockfile
COPY IDHANWeb/ ./
RUN pnpm exec vite build --outDir /web/dist

# Stage 1: Build IDHANServer
FROM --platform=$BUILDPLATFORM ubuntu:25.10 AS builder

ARG BUILDARCH
ARG TARGETARCH
RUN [ "${BUILDARCH}" = "${TARGETARCH}" ] || { \
        echo "IDHAN: cross-architecture builds are not supported (build=${BUILDARCH} target=${TARGETARCH})" >&2; \
        exit 1; \
    }

# universe carries drogon, spdlog, pqxx, tomlplusplus, etc.
RUN sed -i 's/^Components: main$/Components: main universe/' /etc/apt/sources.list.d/ubuntu.sources

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    pkg-config \
    gcc-14 \
    g++-14 \
    ccache \
    libpq-dev \
    liburing-dev \
    libvips-dev \
    libavcodec-dev \
    libavcodec-extra \
    libavfilter-dev \
    libavutil-dev \
    libjsoncpp-dev \
    libjemalloc-dev \
    libfmt-dev \
    libspdlog-dev \
    libarchive-dev \
    libtomlplusplus-dev \
    libc-ares-dev \
    uuid-dev \
    libbrotli-dev \
    libssl-dev \
    libonnxruntime-dev \
    python3

# Set C++23 capable compiler as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

# built manually to get std::source_location to work
RUN git clone --depth 1 --branch 7.10.1 https://github.com/jtv/libpqxx.git /tmp/libpqxx && \
    cmake -S /tmp/libpqxx -B /tmp/libpqxx-build \
        -DCMAKE_CXX_STANDARD=23 \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_DOC=OFF \
        -DBUILD_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    cmake --build /tmp/libpqxx-build -j$(nproc) && \
    cmake --install /tmp/libpqxx-build && \
    rm -rf /tmp/libpqxx /tmp/libpqxx-build

WORKDIR /build

COPY 3rd-party/hydrus /build/3rd-party/hydrus
COPY dependencies /build/dependencies

COPY CMakeLists.txt /build/CMakeLists.txt

COPY IDHAN /build/IDHAN
COPY IDHANModules /build/IDHANModules
COPY IDHANMigration /build/IDHANMigration
COPY IDHANServer /build/IDHANServer

COPY docs /build/docs

COPY .git /build/.git

ARG IDHAN_DISABLE_API_AUTH=OFF
ARG CMAKE_BUILD_TYPE=Release

ARG TARGETVARIANT

ENV CCACHE_DIR=/root/.ccache
RUN --mount=type=cache,target=/root/.ccache,id=idhan-ccache-${TARGETARCH}${TARGETVARIANT} \
    --mount=type=cache,target=/build/build,id=idhan-cmake-${TARGETARCH}${TARGETVARIANT} \
    case "${TARGETVARIANT}" in \
        ""|v1) FGL_MARCH=x86-64 ;; \
        v2)    FGL_MARCH=x86-64-v2 ;; \
        v3)    FGL_MARCH=x86-64-v3 ;; \
        v4)    FGL_MARCH=x86-64-v4 ;; \
        *)     echo "IDHAN: unsupported TARGETVARIANT '${TARGETVARIANT}'" >&2; exit 1 ;; \
    esac && \
    echo "IDHAN: building for ${TARGETARCH}/${TARGETVARIANT:-v1} with -march=${FGL_MARCH}" && \
    git config --global --add safe.directory /build && \
    cmake -S . -B build \
    -UFGL_GIT_BRANCH -UFGL_GIT_COMMIT -UFGL_GIT_TAG \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DFGL_MARCH=${FGL_MARCH} \
    -DCMAKE_CXX_STANDARD=23 \
    -DBUILD_IDHAN_TESTS=OFF \
    -DBUILD_HYDRUS_IMPORTER=OFF \
    -DBUILD_IDHAN_DOCS=ON \
    -DBUILD_IDHAN_WEB=OFF \
    -DBUILD_IDHAN_CLIENT=OFF \
    -DBUILD_IDHAN_TOOLS=OFF \
    -DIDHAN_DISABLE_API_AUTH=${IDHAN_DISABLE_API_AUTH} \
    -DTRANTOR_USE_TLS=none \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build build --target IDHANServer -j$(nproc) && \
    cp /build/build/bin /build/bin -r && \
    find /build/bin -name '*.debug' -delete

# Stage 2: Runtime environment
FROM ubuntu:25.10

RUN sed -i 's/^Components: main$/Components: main universe/' /etc/apt/sources.list.d/ubuntu.sources
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libpq5 \
    libvips42t64 \
    liburing2 \
    libjsoncpp26 \
    libjemalloc2 \
    libuuid1 \
    zlib1g \
    libssl3t64 \
    libc-ares2 \
    libfmt10 \
    libspdlog1.15 \
    libarchive13t64 \
    libtomlplusplus3t64 \
    libonnxruntime1.21 \
    libavcodec-extra61 \
    libavformat61 \
    libavutil59 \
    libswscale8 \
    libswresample5 \
    curl

COPY --from=builder /build/bin/IDHANServer/ /usr/bin/IDHANServer

COPY --from=builder /build/bin/IDHANModuleRunner /usr/bin/IDHANModuleRunner
COPY --from=builder /build/bin/static/ /usr/share/idhan/static
COPY --from=webbuilder /web/dist/ /usr/share/idhan/static
COPY --from=builder /build/bin/modules/ /usr/share/idhan/modules
COPY --from=builder /build/bin/mime/ /usr/share/idhan/mime
COPY --from=builder /build/bin/config.toml /usr/share/idhan/config.toml

# Embedding folder
RUN mkdir -p /usr/share/idhan/models

ENV IDHAN_DATABASE_HOST=localhost \
    IDHAN_DATABASE_USER=idhan \
    IDHAN_DATABASE_PASSWORD=idhan \
    IDHAN_DATABASE_DATABASE=idhan-db \
    IDHAN_THUMBNAILS_PATH=/thumbnails \
    IDHAN_EMBEDDINGS_MODEL_PATH=/usr/share/idhan/models \
    IDHAN_HOST_IPV4_LISTEN=0.0.0.0 \
    IDHAN_HOST_IPV6_LISTEN=:: \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN chmod +x /usr/bin/IDHANServer /usr/bin/IDHANModuleRunner

EXPOSE 16609

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD curl --fail --silent http://localhost:16609/health || exit 1

ENTRYPOINT ["/usr/bin/IDHANServer", "--force_start=true"]
