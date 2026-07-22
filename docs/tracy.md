# Profiling IDHAN with Tracy

Configure: `cmake -DIDHAN_ENABLE_TRACY=ON -B build/tracy && cmake --build build/tracy --target IDHANServer`

Run the server, then open the Tracy profiler GUI and connect (on-demand: collection starts on connect).
Intended for *scenario* profiling — trace one search / one import, capture, analyze — not 24/7.

## Profiling the Docker container

The image builds Tracy in only when asked, and the dev compose publishes the profiler port:

```bash
IDHAN_ENABLE_TRACY=ON docker compose -f docker-compose-dev.yml up --build
```

Then point the Tracy GUI at **`localhost:8086`** (enter the address manually in the connect dialog —
UDP broadcast discovery generally does not cross Docker's bridge network, but the TCP data port is
published so a manual connect works). `TRACY_ON_DEMAND` means the container runs with no profiling
cost until the GUI connects.

Notes:
- `IDHAN_ENABLE_TRACY` defaults to `OFF`, so a plain `docker compose ... up --build` stays lean.
- Port `8086` (tcp + udp) is always published in the dev compose; it is simply idle when the build is
  not Tracy-enabled.
- The dev compose already runs with `seccomp=unconfined`, which io_uring needs; unrelated to Tracy.
- For a tagged/production image (`docker-compose.yml`, prebuilt `:latest`), Tracy is not compiled in —
  build a Tracy image yourself (`docker build --build-arg IDHAN_ENABLE_TRACY=ON ...`) and publish 8086
  if you need to profile that path.

## Coverage

- **IDHAN-owned coroutines** (`IDHANTask`, `ExpectedTask`, `JobTask`) are traced as Tracy fibers via
  hooks in their promise types.
- **`drogon::Task`** (all handlers) is traced too, without ever modifying the vendored drogon
  submodule — see below.

Each coroutine instance is one fiber, so this is *scenario* profiling (thousands of short-lived
fibers accumulate over time); attach, run the operation, capture.

## How drogon::Task is traced without touching the submodule

`await_transform` must be a member of `drogon::Task::promise_type`, which lives in drogon's header.
Rather than editing the submodule, CMake generates a **patched copy** of `coroutine.h` in the build
tree and shadows the original by putting the copy's include dir ahead of drogon's — for both
drogon's own build and its consumers. The submodule stays 100% pristine.

This is wired in `dependencies/Finddrogon.cmake` (guarded by `IDHAN_ENABLE_TRACY`) and happens
automatically at configure time. The patch itself is tracked at
`dependencies/patches/drogon-tracy-fibers.patch`.

**After bumping the drogon submodule:** nothing to do by hand — the shim regenerates on the next
configure. If the patch no longer applies cleanly (drogon changed the promise type), configure
fails with a patch error; update `dependencies/patches/drogon-tracy-fibers.patch` to match the new
`coroutine.h` and reconfigure.

## Why CoroFiber.hpp does not include <tracy/Tracy.hpp>

The shim makes drogon's `coroutine.h` (and thus `idhan_tracy/CoroFiber.hpp`) reachable from almost
every translation unit. If `CoroFiber.hpp` pulled in `<tracy/Tracy.hpp>`, Tracy's heavy client
headers would land in every TU and collide with system/Qt macros (e.g. `BLOCK_SIZE` from
`<linux/*>`). Instead `CoroFiber.hpp` only *declares* `fiberEnter`/`fiberLeave`; the single TU
`dependencies/tracy-coro/src/CoroFiber.cpp` (compiled into the `idhan_tracy_coro` static library) is
the only place that includes `<tracy/Tracy.hpp>`.
