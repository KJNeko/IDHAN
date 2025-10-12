# Multi-stage build for IDHANServer
# Stage 1: Build environment
FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    # Qt6 development packages
    qt6-base-dev \
    qt6-multimedia-dev \
    libqt6core6 \
    libqt6multimedia6 \
    # PostgreSQL client libraries
    libpq-dev \
    postgresql-client \
    # Image processing
    libvips-dev \
    # Async I/O
    liburing-dev \
    # Common dependencies for drogon/trantor
    libjsoncpp-dev \
    uuid-dev \
    zlib1g-dev \
    libssl-dev \
    libc-ares-dev \
    # Compiler with C++23 support
    g++-14 \
    gcc-14 \
    && rm -rf /var/lib/apt/lists/*

# Set C++23 capable compiler as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

WORKDIR /build

# Copy server source files
COPY dependencies/ /build/dependencies/
copy 3rd-party/hydrus /build/3rd-party/hydrus/
COPY IDHANModules/ /build/IDHANModules/
COPY IDHANServer/ /build/IDHANServer/
COPY IDHAN/ /build/IDHAN/
COPY IDHANMigration/ /build/IDHANMigration/

COPY CMakeLists.txt /build/

# Initialize git submodules if needed
RUN if [ -f .gitmodules ]; then git submodule update --init --recursive || true; fi

# Build IDHANServer
RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=23 \
    -DBUILD_IDHAN_TESTS=OFF \
    -DBUILD_HYDRUS_IMPORTER=OFF \
    -DBUILD_IDHAN_DOCS=OFF \
    -DBUILD_IDHAN_WEBUI=OFF \
    -DBUILD_IDHAN_CLIENT=OFF \
    -DBUILD_IDHAN_TOOLS=OFF \
    && cmake --build build --target IDHANServer -j$(nproc)

# Stage 2: Runtime environment
FROM ubuntu:24.04

# Install runtime dependencies only
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
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
    libc-ares2 \
    && rm -rf /var/lib/apt/lists/*

# Create directories
RUN mkdir -p /usr/local/share/idhan/static \
    /var/lib/idhan/modules \
    /etc/idhan/mime.d

# Copy built artifacts from builder stage
COPY --from=builder /build/build/bin/IDHANServer /usr/local/bin/IDHANServer
COPY --from=builder /build/build/bin/static/ /usr/local/share/idhan/static/
COPY --from=builder /build/build/bin/modules/ /var/lib/idhan/modules/
COPY --from=builder /build/build/bin/mime/ /etc/idhan/mime.d/
COPY --from=builder /build/build/bin/config.toml /etc/idhan/config.toml

# Environment variables for database configuration
ENV IDHAN_DATABASE_HOST=localhost \
    IDHAN_DATABASE_USER=idhan \
    IDHAN_DATABASE_PASSWORD=idhan \
    IDHAN_DATABASE_DATABASE=idhan-db

RUN chmod +x /usr/local/bin/IDHANServer

# Make user-managed paths mountable
VOLUME ["/var/lib/idhan/modules", "/etc/idhan/mime.d", "/etc/idhan"]

# Expose default port (adjust based on your config)
EXPOSE 16609

# Default entrypoint
ENTRYPOINT ["/usr/local/bin/IDHANServer"]
