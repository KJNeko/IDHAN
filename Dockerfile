# Multi-stage build for IDHANServer
# Stage 1: Build environment
FROM ubuntu:24.04 AS builder

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
    libjsoncpp-dev

# Set C++23 capable compiler as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

WORKDIR /build

# Copy dependencies first to maximize cache hits
COPY 3rd-party/hydrus /build/3rd-party/hydrus
COPY dependencies /build/dependencies

# Copy the rest of the source code
COPY CMakeLists.txt /build/CMakeLists.txt
COPY IDHAN /build/IDHAN
COPY IDHANModules /build/IDHANModules
COPY IDHANMigration /build/IDHANMigration
COPY IDHANServer /build/IDHANServer
COPY docs /build/docs

# Build IDHANServer with ccache mount
ENV CCACHE_DIR=/root/.ccache
RUN --mount=type=cache,target=/root/.ccache \
    --mount=type=cache,target=/build/build \
    cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=23 \
    -DBUILD_IDHAN_TESTS=OFF \
    -DBUILD_HYDRUS_IMPORTER=OFF \
    -DBUILD_IDHAN_DOCS=ON \
    -DBUILD_IDHAN_WEBUI=OFF \
    -DBUILD_IDHAN_CLIENT=OFF \
    -DBUILD_IDHAN_TOOLS=OFF \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && \
    cmake --build build --target IDHANServer -j$(nproc) && \
    cp /build/build/bin /build/bin -r

# Stage 2: Runtime environment
FROM ubuntu:24.04

# Install runtime dependencies and setup locale in one layer
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
    ffmpeg \
    locales && \
    locale-gen en_US.UTF-8 && \
    update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# Copy built artifacts from builder stage
COPY --from=builder /build/bin/IDHANServer/ /usr/bin/IDHANServer
COPY --from=builder /build/bin/static/ /usr/share/idhan/static
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

# Default entrypoint
ENTRYPOINT ["/usr/bin/IDHANServer", "--force_start=true"]
