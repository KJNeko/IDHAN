# CMake organization

The root `CMakeLists.txt` orders components. Configuration belongs in:

- `cmake/ProjectSettings.cmake`: language standard and shared build defaults.
- `cmake/Options.cmake`: public options, defaults, and component dependencies.
- `cmake/Dependencies.cmake`: packages shared by components and coroutine setup.
- Component `CMakeLists.txt`: target creation, sources, and usage requirements.
- Component `cmake/`: asset generation and other substantial build steps.
- `tests/<suite>/CMakeLists.txt`: each suite's targets and dependencies.

Use `AddFGLLibrary`, `AddFGLExecutable`, and `AddFGLModule` from libFGL for
C++ targets that need the project's warnings, reflection flags, and build
metadata. Use native CMake targets for interface libraries and generated objects.
Keep compiler options, definitions, include paths, and Qt autogen properties on
targets. Link imported package targets rather than copying their include/library
variables. Executables use PRIVATE dependencies; libraries expose dependencies
needed by their public headers through PUBLIC or INTERFACE.

## Configuring

Initialize submodules with `git submodule update --init --recursive`, then reuse
an existing build directory or configure a new one:

```sh
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --target IDHANServer
```

CMake 3.28 or newer and a C++26 compiler are required. HTTP also requires C++26
reflection. `Debug`, `Release`, `RelWithDebInfo`, and the libFGL `System`
configuration are supported. Dependency fetching retains its existing behavior;
for offline builds, provide installed packages or CMake FetchContent source
overrides for the pinned dependencies.

Existing `BUILD_IDHAN_*`, `BUILD_HYDRUS_IMPORTER`, and `IDHAN_*` cache option names
remain supported. Tests and docs enable the server, which enables HTTP and the
downloader. The downloader tester enables the downloader; tools and the importer
enable the Qt client. Dependency resolution reaches the complete graph even
when several prerequisite options were explicitly disabled. HTTP can also be
built without the server. `BUILD_IDHAN_TOOLS` retains its existing behavior of
building the importer. `BUILD_PTR_CORE_TESTS` can build storage tests without Qt.

For a client-only configuration, disable server, docs, HTTP, downloader,
downloader tester, WebUI, tools, and importer, and enable `BUILD_IDHAN_CLIENT`.
`IDHAN_ENABLE_TRACE` defaults OFF for a fresh Debug cache and ON otherwise;
explicit values are respected. Hardening defaults remain OFF for Debug and ON
otherwise. These defaults use `CMAKE_BUILD_TYPE`; multi-configuration generators
should set these options explicitly when required.

## Generated artifacts and tests

Hydrus headers are generated into `<build>/IDHAN/generated/hydrus` and propagated
through the IDHAN target. Configure never clones Hydrus into the source tree.
The migration source remains in `<build>/IDHANMigration` and regenerates when
its SQL, templates, or generator change. Generation does not apply migrations.
The OpenAPI snapshot and WebUI/parser staging retain their existing behavior.

Executables remain in `<build>/bin`, and loadable modules in `bin/modules`.
Test support files now live under `<build>/tests/<suite>`.

```sh
cmake -S . -B build/debug -DBUILD_IDHAN_TESTS=ON
cmake --build build/debug --target IDHANSharedTests
ctest --test-dir build/debug/tests/shared --output-on-failure
```

Enable tests with `BUILD_IDHAN_TESTS`; CTest is enabled at the project root.
Database/API suites require PostgreSQL as described in `tests/api/README.md`.
Component option checks can also run without compiling or finding dependencies:

```sh
cmake -DIDHAN_OPTIONS_CASE=tests -P tests/cmake/Options.cmake
cmake -DIDHAN_OPTIONS_CASE=docs -P tests/cmake/Options.cmake
cmake -DIDHAN_OPTIONS_CASE=http -P tests/cmake/Options.cmake
cmake -DIDHAN_OPTIONS_CASE=trace -P tests/cmake/Options.cmake
```
