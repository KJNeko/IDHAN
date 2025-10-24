# Multi-stage build for IDHANServer
# Stage 1: Build environment
FROM ubuntu:24.04 AS builder

RUN apt-get update && DEBIAN_FRONTNED=noninteractive apt-get install -y \
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
    libqt6core6 \
    libqt6multimedia6 \
    libjsoncpp-dev \
    libvips-dev

# Set C++23 capable compiler as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

RUN rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY . /build/

# Initialize git submodules if needed
RUN if [ -f .gitmodules ]; then git submodule update --init --recursive || true; fi

# Build IDHANServer
RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=23 \
    -DBUILD_IDHAN_TESTS=OFF \
    -DBUILD_HYDRUS_IMPORTER=OFF \
    -DBUILD_IDHAN_DOCS=ON \
    -DBUILD_IDHAN_WEBUI=OFF \
    -DBUILD_IDHAN_CLIENT=OFF \
    -DBUILD_IDHAN_TOOLS=OFF

ENV CCACHE_DIR=/root/.ccache
RUN --mount=type=cache,target=/root/.ccache \
    cmake --build build --target IDHANServer -j$(nproc)

# Stage 2: Runtime environment
FROM ubuntu:24.04

RUN apt-get update

# Install runtime dependencies only
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y \
    # Qt6 runtime libraries
    libqt6core6 \
    libqt6multimedia6 \
    # PostgreSQL client
    libpq5 \
    # Image processing
    libvips42 \
    # Async I/O
    liburing2 \
    # Supporting libraries
    libjsoncpp25 \
    uuid-runtime \
    zlib1g \
    libssl3 \
    libc-ares2

# Cleanup
RUN apt-get clean

RUN rm -rf /var/lib/apt/lists/*

# Create directories
RUN mkdir -p /usr/share/idhan/

# Copy built artifacts from builder stage
COPY --from=builder /build/build/bin/IDHANServer/ /usr/bin/IDHANServer
COPY --from=builder /build/build/bin/static/ /usr/share/idhan/static
COPY --from=builder /build/build/bin/modules/ /usr/share/idhan/modules
COPY --from=builder /build/build/bin/mime/ /usr/share/idhan/mime
COPY --from=builder /build/build/bin/config.toml /usr/share/idhan/config.toml

# Environment variables for database configuration
ENV IDHAN_DATABASE_HOST=localhost \
    IDHAN_DATABASE_USER=idhan \
    IDHAN_DATABASE_PASSWORD=idhan \
    IDHAN_DATABASE_DATABASE=idhan-db

RUN chmod +x /usr/bin/IDHANServer

# Expose default port (adjust based on your config)
EXPOSE 16609

# Default entrypoint
ENTRYPOINT ["/usr/bin/IDHANServer", "--localhost_only", "false"]
