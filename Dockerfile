# syntax=docker/dockerfile:1.7
# Multi-stage build for IDHANServer
#
# Built for three x86-64 microarchitecture levels -- linux/amd64, linux/amd64/v2 and
# linux/amd64/v3 -- which land in one manifest list per tag. The only difference between
# them is the -march the server is compiled with, derived from TARGETVARIANT below.
#
# Stage 1: Build environment
# Stage 0: Build the React WebUI
#
# Pinned to BUILDPLATFORM: the output is a JavaScript bundle with no machine code in it,
# so it is identical across the three variants. Without the pin this stage, and the pnpm
# install in front of it, would run once per variant for three identical results.
FROM --platform=$BUILDPLATFORM node:22-slim AS webbuilder
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
# 25.10 rather than an LTS: libonnxruntime-dev, which gates IDHANEmbedding, first appears in
# questing. No Ubuntu release before it packages ONNX Runtime at all.
#
# Pinned to BUILDPLATFORM. The three published variants differ only in the -march the compiler is
# given, not in the architecture it targets, so this stage is a native compile in every case and
# nothing here needs to run under the target's instruction set -- which also means the build host
# does not have to support v3 to produce a v3 image. Pinning collapses the apt install, the libpqxx
# build and the ~1 GB Qt fetch to a single set of layers shared by all three variants instead of
# three identical copies keyed by platform. Only the final build step, which reads TARGETVARIANT,
# has a distinct cache key per variant.
FROM --platform=$BUILDPLATFORM ubuntu:25.10 AS builder

# Guard on the BUILDPLATFORM pin above: it is only sound while the build host and the target share
# an architecture. Building amd64 images on an arm64 host would need a cross toolchain that is not
# installed here, and would otherwise produce arm64 binaries wearing an amd64 label.
ARG BUILDARCH
ARG TARGETARCH
RUN [ "${BUILDARCH}" = "${TARGETARCH}" ] || { \
        echo "IDHAN: cross-architecture builds are not supported (build=${BUILDARCH} target=${TARGETARCH})" >&2; \
        exit 1; \
    }

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
    libonnxruntime-dev \
    python3 \
    python3-pip

# Set C++23 capable compiler as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

# Build libpqxx from source: 24.04 shipped 7.8.1 compiled without C++23 std::source_location
# support, causing undefined symbol errors at link time. 25.10 packages 7.10.0 -- the same version
# built here -- so this stage may now be redundant, but whether the packaged build carries the same
# defect is untested. Left in place until someone checks.
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
COPY IDHANServer /build/IDHANServer

# Copy docs and remaining
COPY docs /build/docs

# Copy the top-level git metadata so FGLGit can derive the /version endpoint's branch/commit/tag in
# the container — no host-side build args required. .dockerignore drops .git/modules (the multi-GB
# submodule histories); the parent repo's `git describe` doesn't need them, so this stays ~24 MB.
# Copied last so a new commit only invalidates the build layer below, not the dependency install.
COPY .git /build/.git

# Build IDHANServer with ccache mount
ARG IDHAN_DISABLE_API_AUTH=OFF
ARG CMAKE_BUILD_TYPE=Release

# The microarchitecture level this image is being built for. Buildx sets TARGETVARIANT from the
# requested --platform: empty for linux/amd64, "v2" for linux/amd64/v2, "v3" for linux/amd64/v3.
# It is translated to an -march level below rather than passed through, since the two spellings do
# not match.
ARG TARGETVARIANT

ENV CCACHE_DIR=/root/.ccache
# safe.directory: the copied .git is root-owned like the build user, but declare it explicitly so
# git never refuses with "dubious ownership" under a different build UID.
# -UFGL_GIT_*: the /build/build cache mount persists CMakeCache.txt across builds. An earlier image
# built with the old -DFGL_GIT_*=unknown args left those overrides cached; reconfiguring without -D
# does NOT clear them, so FGLGit would keep taking the stale-override path and skip git. -U removes
# them each configure, forcing in-container `git describe` to win.
#
# Both cache mounts are keyed by variant. The build directory is the one that matters: it persists
# CMakeCache.txt, so a mount shared across variants would hand the v3 configure the v2 cache and
# FGL_MARCH would survive the reconfigure -- the same stale-cache trap the -U above exists for,
# except the symptom is a mislabelled binary rather than a missing version string. ccache is keyed
# too because its entries are per-flag-set anyway; sharing one would only mix three working sets
# into a single eviction pool.
#
# An unrecognised variant fails the build rather than falling back to baseline. The reason all three
# of these exist is that the binary should match the platform the manifest advertises, and a silent
# fallback would reintroduce exactly the mismatch being removed.
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
    cp /build/build/bin /build/bin -r

# Stage 2: Runtime environment
FROM ubuntu:25.10

# Install runtime dependencies and setup locale in one layer
RUN sed -i 's/^Components: main$/Components: main universe/' /etc/apt/sources.list.d/ubuntu.sources
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libqt6core6t64 \
    libqt6multimedia6 \
    libpq5 \
    libvips42t64 \
    liburing2 \
    libjsoncpp26 \
    uuid-runtime \
    zlib1g \
    libssl3t64 \
    libc-ares2 \
    libfmt10 \
    libspdlog1.15 \
    libarchive13t64 \
    libtomlplusplus3t64 \
    libonnxruntime1.21 \
    ffmpeg \
    curl \
    locales

RUN locale-gen en_US.UTF-8 && \
    update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# Copy built artifacts from builder stage
COPY --from=builder /build/bin/IDHANServer/ /usr/bin/IDHANServer
# Modules run out of process; the server spawns one of these per module library. Without it every
# library is skipped at startup and no metadata parser or thumbnailer exists at all.
COPY --from=builder /build/bin/IDHANModuleRunner /usr/bin/IDHANModuleRunner
COPY --from=builder /build/bin/static/ /usr/share/idhan/static
COPY --from=webbuilder /web/dist/ /usr/share/idhan/static
COPY --from=builder /build/bin/modules/ /usr/share/idhan/modules
COPY --from=builder /build/bin/mime/ /usr/share/idhan/mime
COPY --from=builder /build/bin/config.toml /usr/share/idhan/config.toml
# No embedding model is baked in. The directory is left empty and
# IDHAN_EMBEDDINGS_MODEL_PATH still points at it, so mounting a model there is the only step:
#
#   volumes:
#     - /path/to/siglip2-base-patch16-224:/usr/share/idhan/models/siglip2-base-patch16-224
#
# One subdirectory per model, in the layout onnx-community publishes (onnx/vision_model.onnx,
# onnx/text_model.onnx, and the tokenizer/preprocessor json beside them). An absent or empty
# directory is not an error: the server logs that no embedding model is available and everything
# else carries on. See docs/docker.md.
RUN mkdir -p /usr/share/idhan/models

# Environment variables for database configuration
ENV IDHAN_DATABASE_HOST=localhost \
    IDHAN_DATABASE_USER=idhan \
    IDHAN_DATABASE_PASSWORD=idhan \
    IDHAN_DATABASE_DATABASE=idhan-db \
    IDHAN_THUMBNAILS_PATH=/thumbnails \
    IDHAN_EMBEDDINGS_MODEL_PATH=/usr/share/idhan/models \
    IDHAN_HOST_IPV4_LISTEN=0.0.0.0 \
    IDHAN_HOST_IPV6_LISTEN=:: \
    LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

RUN chmod +x /usr/bin/IDHANServer /usr/bin/IDHANModuleRunner

EXPOSE 16609

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD curl --fail --silent http://localhost:16609/health || exit 1

# Default entrypoint
ENTRYPOINT ["/usr/bin/IDHANServer", "--force_start=true"]
