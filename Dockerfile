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
# 25.10 rather than an LTS: libonnxruntime-dev, which gates IDHANEmbedding, first appears in
# questing. No Ubuntu release before it packages ONNX Runtime at all.
FROM ubuntu:25.10 AS builder

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
ENV CCACHE_DIR=/root/.ccache
# safe.directory: the copied .git is root-owned like the build user, but declare it explicitly so
# git never refuses with "dubious ownership" under a different build UID.
# -UFGL_GIT_*: the /build/build cache mount persists CMakeCache.txt across builds. An earlier image
# built with the old -DFGL_GIT_*=unknown args left those overrides cached; reconfiguring without -D
# does NOT clear them, so FGLGit would keep taking the stale-override path and skip git. -U removes
# them each configure, forcing in-container `git describe` to win.
RUN --mount=type=cache,target=/root/.ccache \
    --mount=type=cache,target=/build/build \
    git config --global --add safe.directory /build && \
    cmake -S . -B build \
    -UFGL_GIT_BRANCH -UFGL_GIT_COMMIT -UFGL_GIT_TAG \
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

# Stage 2: Export the embedding model to ONNX
# The embedding module loads ONNX and nothing else -- it has no torch in it and never will. The
# checkpoint on the Hub is PyTorch, so the conversion has to happen somewhere; doing it in a stage
# of its own keeps ~2.5 GB of torch out of both the runtime image and the developer's machine. Only
# the two output files cross into the runtime stage.
FROM python:3.13-slim AS modelbuilder

ARG EMBEDDING_MODEL=ViT-B-16-SigLIP2

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends curl ca-certificates

# The CPU index, not PyPI: the default linux torch wheel bundles CUDA and is several GB of dead
# weight for a one-shot export that runs on the build machine's CPU. Separate from the second
# install so PyPI cannot reintroduce the CUDA build as a dependency resolution.
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install --index-url https://download.pytorch.org/whl/cpu torch torchvision
RUN --mount=type=cache,target=/root/.cache/pip \
    pip install open_clip_torch onnx

# Fetched file-by-file rather than `git clone`: the repo carries the same weights twice --
# open_clip_model.safetensors and open_clip_pytorch_model.bin, ~1.5 GB each -- and git-lfs has no way
# to skip one, so a clone pulls 3.0 GB to land 1.5 GB of useful bytes. open_clip prefers safetensors.
WORKDIR /checkpoint
RUN mkdir -p ${EMBEDDING_MODEL} && cd ${EMBEDDING_MODEL} && \
    for file in open_clip_config.json open_clip_model.safetensors; do \
        curl -fL --retry 3 --retry-delay 2 -o "$file" \
            "https://huggingface.co/timm/${EMBEDDING_MODEL}/resolve/main/$file"; \
    done

# Writes model.json from open_clip's own preprocess_cfg rather than from anything hand-copied, which
# is the reason this runs a script instead of a curl.
COPY tools/embedding-export/export_siglip2.py /export.py
RUN python /export.py --model-dir /checkpoint/${EMBEDDING_MODEL} --out /models

# Stage 3: Runtime environment
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
# Just model.onnx and model.json -- the checkpoint and torch stay behind in the export stage.
COPY --from=modelbuilder /models/ /usr/share/idhan/models

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
