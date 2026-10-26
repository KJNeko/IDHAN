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
FROM --platform=$BUILDPLATFORM ubuntu:26.10 AS builder

ARG BUILDARCH
ARG TARGETARCH
RUN [ "${BUILDARCH}" = "${TARGETARCH}" ] || { \
        echo "IDHAN: cross-architecture builds are not supported (build=${BUILDARCH} target=${TARGETARCH})" >&2; \
        exit 1; \
    }

RUN sed -i 's/^Components: main$/Components: main universe/' /etc/apt/sources.list.d/ubuntu.sources

# Pinned due to openssl changing something that causes folly to eat shit when compiling
RUN echo "deb http://archive.ubuntu.com/ubuntu questing main universe" > /etc/apt/sources.list.d/questing.list && \
    printf 'Package: libssl-dev libssl3t64\nPin: release n=questing\nPin-Priority: 1001\n' > /etc/apt/preferences.d/openssl-questing

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends --allow-downgrades \
    build-essential \
    cmake \
    git \
    ca-certificates \
    pkg-config \
    gcc-16 \
    g++-16 \
    ninja-build \
    gperf \
    ccache \
    libpq-dev \
    liburing-dev \
    libvips-dev \
    libavcodec-dev \
    libavcodec-extra \
    libavfilter-dev \
    libavutil-dev \
    libavformat-dev \
    libswscale-dev \
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
    libcurl4-openssl-dev \
    libonnxruntime-dev \
    libboost-context-dev \
    libboost-filesystem-dev \
    libboost-program-options-dev \
    libboost-regex-dev \
    libfast-float-dev \
    libdouble-conversion-dev \
    libgflags-dev \
    libgoogle-glog-dev \
    libevent-dev \
    libsodium-dev \
    libzstd-dev \
    liblz4-dev \
    libsnappy-dev \
    libbz2-dev \
    liblzma-dev \
    libunwind-dev \
    libdwarf-dev \
    libiberty-dev \
    libaio-dev \
    zlib1g-dev \
    python3 \
    nodejs \
    npm \
    default-jre-headless && \
    rm -rf /var/lib/apt/lists/*

# GCC 15 supports the C++26 dialect used by IDHAN and its coroutine dependencies.
ENV CC=gcc-16 CXX=g++-16
ARG BUILD_JOBS=4

# built manually to get std::source_location to work
RUN git clone --depth 1 --branch 7.10.1 https://github.com/jtv/libpqxx.git /tmp/libpqxx && \
    cmake -S /tmp/libpqxx -B /tmp/libpqxx-build \
        -DCMAKE_CXX_STANDARD=23 \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_DOC=OFF \
        -DBUILD_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    cmake --build /tmp/libpqxx-build -j${BUILD_JOBS} && \
    cmake --install /tmp/libpqxx-build && \
    rm -rf /tmp/libpqxx /tmp/libpqxx-build

WORKDIR /build

COPY 3rd-party/hydrus /build/3rd-party/hydrus
COPY dependencies /build/dependencies

COPY CMakeLists.txt /build/CMakeLists.txt

COPY IDHAN /build/IDHAN
COPY IDHANDb /build/IDHANDb
COPY IDHANHttp /build/IDHANHttp
COPY IDHANDownloader /build/IDHANDownloader
COPY IDHANModules /build/IDHANModules
COPY IDHANMigration /build/IDHANMigration
COPY IDHANServer /build/IDHANServer

COPY docs /build/docs

COPY .git /build/.git

ARG IDHAN_DISABLE_API_AUTH=OFF
ARG CMAKE_BUILD_TYPE=Release

ARG TARGETVARIANT

ENV CCACHE_DIR=/root/.ccache
# The npm cache also holds the openapi-generator jar that npx fetches for the downloader specs.
RUN --mount=type=cache,target=/root/.ccache,id=idhan-ccache-${TARGETARCH}${TARGETVARIANT} \
    --mount=type=cache,target=/build/build,id=idhan-cmake-ubuntu2604-gcc15-http-${TARGETARCH}${TARGETVARIANT} \
    --mount=type=cache,target=/root/.npm,id=idhan-npm-${TARGETARCH}${TARGETVARIANT} \
    case "${TARGETARCH}/${TARGETVARIANT}" in \
        amd64/|amd64/v1) FGL_MARCH=x86-64 ;; \
        amd64/v2)       FGL_MARCH=x86-64-v2 ;; \
        amd64/v3)       FGL_MARCH=x86-64-v3 ;; \
        amd64/v4)       FGL_MARCH=x86-64-v4 ;; \
        arm64/|arm64/v8) FGL_MARCH=armv8-a ;; \
        *) echo "IDHAN: unsupported target '${TARGETARCH}/${TARGETVARIANT}'" >&2; exit 1 ;; \
    esac && \
    echo "IDHAN: building for ${TARGETARCH}/${TARGETVARIANT:-v1} with -march=${FGL_MARCH}" && \
    git config --global --add safe.directory /build && \
    cmake -S . -B build -G Ninja \
    -UFGL_GIT_BRANCH -UFGL_GIT_COMMIT -UFGL_GIT_TAG \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DFGL_MARCH=${FGL_MARCH} \
    -DCMAKE_CXX_STANDARD=26 \
    -DBUILD_IDHAN_TESTS=OFF \
    -DBUILD_HYDRUS_IMPORTER=OFF \
    -DBUILD_IDHAN_DOCS=ON \
    -DBUILD_IDHAN_WEB=OFF \
    -DBUILD_IDHAN_CLIENT=OFF \
    -DBUILD_IDHAN_TOOLS=OFF \
    -DIDHAN_DISABLE_API_AUTH=${IDHAN_DISABLE_API_AUTH} \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build build --target IDHANServer -j${BUILD_JOBS} && \
    cp /build/build/bin /build/bin -r && \
    find /build/bin -name '*.debug' -delete

# Stage 2: Runtime environment
FROM ubuntu:26.10

RUN sed -i 's/^Components: main$/Components: main universe/' /etc/apt/sources.list.d/ubuntu.sources
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates \
    libpq5 \
    libvips42t64 \
    liburing2 \
    libjsoncpp26 \
    libjemalloc2 \
    libuuid1 \
    zlib1g \
    libssl3t64 \
    libcurl4t64 \
    libc-ares2 \
    libfmt10 \
    libspdlog1.15 \
    libarchive13t64 \
    libtomlplusplus3t64 \
    libonnxruntime1.23 \
    libavcodec-extra62 \
    libavformat62 \
    libavutil60 \
    libswscale9 \
    libswresample6 \
    libboost-context1.90.0 \
    libboost-filesystem1.90.0 \
    libboost-program-options1.90.0 \
    libboost-regex1.90.0 \
    libdouble-conversion3 \
    libgflags2.2 \
    libgoogle-glog0v6t64 \
    libevent-2.1-7t64 \
    libevent-openssl-2.1-7t64 \
    libsodium26 \
    libzstd1 \
    liblz4-1 \
    libsnappy1v5 \
    libbz2-1.0 \
    liblzma5 \
    libunwind8 \
    libdwarf1 \
    libaio1t64 \
    libstdc++6 \
    curl && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/bin/IDHANServer /usr/bin/IDHANServer

COPY --from=builder /build/bin/IDHANModuleRunner /usr/bin/IDHANModuleRunner
COPY --from=builder /build/bin/static/ /usr/share/idhan/static
COPY --from=webbuilder /web/dist/ /usr/share/idhan/static
COPY --from=builder /build/bin/modules/ /usr/share/idhan/modules
COPY --from=builder /build/bin/config.toml /usr/share/idhan/config.toml
COPY --from=builder /build/bin/downloader/ /usr/share/idhan/downloader

# Embedding folder
RUN mkdir -p /usr/share/idhan/models

ENV IDHAN_DATABASE_HOST=localhost \
    IDHAN_DATABASE_USER=idhan \
    IDHAN_DATABASE_PASSWORD=idhan \
    IDHAN_DATABASE_DATABASE=idhan-db \
    IDHAN_THUMBNAILS_PATH=/thumbnails \
    IDHAN_EMBEDDINGS_MODEL_PATH=/usr/share/idhan/models \
    IDHAN_DOWNLOADER_PARSER_DIRECTORY=/usr/share/idhan/downloader \
    IDHAN_HOST_IPV4_LISTEN=0.0.0.0 \
    IDHAN_HOST_IPV6_LISTEN=:: \
    IDHAN_HOST_USE_TLS=false \
    IDHAN_SERVER_TEMP_PATH=/tmp/idhan \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN chmod +x /usr/bin/IDHANServer /usr/bin/IDHANModuleRunner

# Check the server, runner, and modules: modules are loaded in separate processes.
RUN set -eu; \
    for binary in /usr/bin/IDHANServer /usr/bin/IDHANModuleRunner /usr/share/idhan/modules/*.so; do \
        ldd "$binary" > /tmp/idhan-ldd.txt; \
        if grep -q 'not found' /tmp/idhan-ldd.txt; then \
            cat /tmp/idhan-ldd.txt; exit 1; \
        fi; \
    done; \
    rm /tmp/idhan-ldd.txt

EXPOSE 16609

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD curl --fail --silent http://localhost:16609/health || exit 1

ENTRYPOINT ["/usr/bin/IDHANServer", "--force_start=true"]
