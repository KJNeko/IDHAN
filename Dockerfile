# Multi-stage build for IDHANServer
# Stage 1: Build environment
# Stage 0: Build the React WebUI
FROM node:22-slim AS webbuilder
# corepack activates the exact pnpm from package.json's "packageManager" field (pinned to 10.x, where
# the onlyBuiltDependencies allow-list actually works). Disable the prompt so the fetch is non-interactive.
ENV COREPACK_ENABLE_DOWNLOAD_PROMPT=0
RUN corepack enable
WORKDIR /web
# pnpm-workspace.yaml carries onlyBuiltDependencies (esbuild), without which pnpm 10+ aborts the
# install on an ignored build script — so it must be present before `pnpm install`, not just at build.
COPY IDHANWeb/package.json IDHANWeb/pnpm-lock.yaml IDHANWeb/pnpm-workspace.yaml ./
RUN --mount=type=cache,target=/pnpm-store \
    pnpm config set store-dir /pnpm-store && pnpm install --frozen-lockfile
COPY IDHANWeb/ ./
RUN pnpm exec vite build --outDir /web/dist

# Stage 1: Build IDHANServer
FROM ubuntu:24.04 AS builder

# Enable universe repository (needed for drogon, spdlog, pqxx, tomlplusplus, etc.)
RUN sed -i 's/^Components: main$/Components: main universe/' /etc/apt/sources.list.d/ubuntu.sources

# Use build mounts for apt cache to speed up re-builds
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    pkg-config \
    gcc-14 \
    g++-14 \
    ccache \
    libpq-dev \
    liburing-dev \
    qt6-base-dev \
    qt6-multimedia-dev \
    libvips-dev \
    libavcodec-dev \
    libavcodec-extra \
    libavfilter-dev \
    libavutil-dev \
    libjsoncpp-dev \
    libfmt-dev \
    libspdlog-dev \
    libarchive-dev \
    libtomlplusplus-dev \
    libc-ares-dev \
    uuid-dev \
    libbrotli-dev \
    libssl-dev \
    python3 \
    python3-pip

# Set C++23 capable compiler as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

# Build libpqxx from source: Ubuntu 24.04 ships 7.8.1 compiled without C++23
# std::source_location support, causing undefined symbol errors at link time.
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

RUN pip3 install aqtinstall --break-system-packages

RUN aqt install-qt linux desktop 6.11.1 linux_gcc_64 --outputdir /opt/qt6

WORKDIR /build

# Copy dependencies first (rarely changes)
COPY 3rd-party/hydrus /build/3rd-party/hydrus
COPY dependencies /build/dependencies

# Copy CMakeLists.txt
COPY CMakeLists.txt /build/CMakeLists.txt

# Copy core libraries
COPY IDHAN /build/IDHAN
COPY IDHANModules /build/IDHANModules
COPY IDHANMigration /build/IDHANMigration

# Copy server (most frequently changed source)
COPY IDHANServer /build/IDHANServer

# Copy docs and remaining
COPY docs /build/docs

# Build IDHANServer with ccache mount
ARG IDHAN_DISABLE_API_AUTH=OFF
ARG CMAKE_BUILD_TYPE=Release
ENV CCACHE_DIR=/root/.ccache
RUN --mount=type=cache,target=/root/.ccache \
    --mount=type=cache,target=/build/build \
    cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
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
    cp /build/build/bin /build/bin -r

# Stage 2: Runtime environment
FROM ubuntu:24.04

# Install runtime dependencies and setup locale in one layer
RUN sed -i 's/^Components: main$/Components: main universe/' /etc/apt/sources.list.d/ubuntu.sources
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libqt6core6 \
    libqt6multimedia6 \
    libpq5 \
    libvips42 \
    liburing2 \
    libjsoncpp25 \
    uuid-runtime \
    zlib1g \
    libssl3 \
    libc-ares2 \
    libfmt9 \
    libspdlog1.12 \
    libarchive13 \
    libtomlplusplus3t64 \
    ffmpeg \
    curl \
    locales && \
    locale-gen en_US.UTF-8 && \
    update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# Copy built artifacts from builder stage
COPY --from=builder /build/bin/IDHANServer/ /usr/bin/IDHANServer
COPY --from=builder /build/bin/static/ /usr/share/idhan/static
COPY --from=webbuilder /web/dist/ /usr/share/idhan/static
COPY --from=builder /build/bin/modules/ /usr/share/idhan/modules
COPY --from=builder /build/bin/mime/ /usr/share/idhan/mime
COPY --from=builder /build/bin/config.toml /usr/share/idhan/config.toml

# Environment variables for database configuration
ENV IDHAN_DATABASE_HOST=localhost \
    IDHAN_DATABASE_USER=idhan \
    IDHAN_DATABASE_PASSWORD=idhan \
    IDHAN_DATABASE_DATABASE=idhan-db \
    IDHAN_THUMBNAILS_PATH=/thumbnails \
    IDHAN_HOST_IPV4_LISTEN=0.0.0.0 \
    IDHAN_HOST_IPV6_LISTEN=:: \
    LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

RUN chmod +x /usr/bin/IDHANServer

# Expose default port
EXPOSE 16609

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD curl --fail --silent http://localhost:16609/health || exit 1

# Default entrypoint
ENTRYPOINT ["/usr/bin/IDHANServer", "--force_start=true"]
