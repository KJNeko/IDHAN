# Dependencies

## Bundled submodules

| Dependency | Path | Version | Purpose |
|---|---|---|---|
| [drogon](https://github.com/drogonframework/drogon) | `dependencies/drogon` | 1.9.13 | HTTP application framework (HttpController, coroutine filters, async DB client) |
| [libFGL](https://git.futuregadgetlabs.net/KJ16609/libFGL) | `dependencies/libFGL` | in-tree | CMake helpers: `AddFGLLibrary`, `AddFGLExecutable`, `AddFGLModule` |
| [chardet](https://github.com/Joungkyun/libchardet) | `dependencies/chardet` | in-tree | Character encoding detection for metadata extraction |
| [hydrus](https://github.com/hydrusnetwork/hydrus) | `3rd-party/hydrus` | in-tree | Source of Hydrus constants; scanned at CMake configure time to generate `IDHAN/include/hydrus/*_gen.hpp` |
| [hydrus-web](https://github.com/floogulinc/hydrus-web) | `3rd-party/hydrus-web` | in-tree | Hydrus web client bundled as a static asset |
| [hydrui](https://github.com/hydrui/hydrui) | `3rd-party/hydrui` | in-tree | Alternative web UI bundled as a static asset |
| [doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css) | `docs/doxygen-awesome-css` | in-tree | Doxygen theme |

## System packages (Ubuntu 24.04 / `apt`)

| Dependency | Package | Purpose |
|---|---|---|
| fmt | `libfmt-dev` / `libfmt9` | String formatting (pulled in transitively by spdlog) |
| spdlog | `libspdlog-dev` / `libspdlog1.12` | Structured logging throughout the server and modules |
| libpqxx | `libpqxx-dev` / `libpqxx-7.8t64` | C++ PostgreSQL client used for all direct DB access |
| libarchive | `libarchive-dev` / `libarchive13` | Archive extraction in IDHANPremadeModules |
| tomlplusplus | `libtomlplusplus-dev` / `libtomlplusplus3t64` | Config file parsing |
| jsoncpp | `libjsoncpp-dev` / `libjsoncpp25` | JSON serialisation (also a drogon dependency) |
| libpq | `libpq-dev` / `libpq5` | PostgreSQL C client library (used by drogon async DB and libpqxx) |
| liburing | `liburing-dev` / `liburing2` | io_uring async I/O for file storage layer (Linux only) |
| Qt6 | `qt6-base-dev`, `qt6-multimedia-dev` | IDHANClient shared library and HydrusImporter GUI |
| libvips | `libvips-dev` / `libvips42` | Image processing in the vips thumbnailer module |
| FFmpeg | `libavcodec-dev`, `libavfilter-dev`, `libavutil-dev` | Video/audio metadata and thumbnailing module |
| OpenSSL | `libssl-dev` / `libssl3` | TLS and SHA-256 hashing |
| SQLite3 | `libsqlite3-dev` | HydrusImporter reads Hydrus SQLite databases |
| zlib | `zlib1g-dev` / `zlib1g` | Compression support |
