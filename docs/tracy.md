# Profiling IDHAN with Tracy

Configure: `cmake -DIDHAN_ENABLE_TRACY=ON -B build/tracy && cmake --build build/tracy --target IDHANServer`

Run the server, then open the Tracy profiler GUI and connect (on-demand: collection starts on connect).
Intended for *scenario* profiling — trace one search / one import, capture, analyze — not 24/7.

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
