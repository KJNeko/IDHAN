# Tracy Profiling Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate the Tracy frame profiler into IDHANServer with full coroutine timelines that survive Drogon's cross-thread suspend/resume, plus synchronous zone instrumentation on CPU hot paths — all zero-footprint when disabled.

**Architecture:** A compile-time `IDHAN_ENABLE_TRACY` option vendors Tracy and links `Tracy::TracyClient` publicly onto both `IDHANServer` and the `drogon` target (ODR consistency). Coroutine promises (`IDHANTask`, `JobTaskPromise`, and patched `drogon::Task`) gain fiber hooks via `await_transform` + custom initial/final awaiters, so each coroutine instance becomes one Tracy fiber whose zone stack is saved/restored across thread hops. Synchronous leaves get plain `ZoneScoped`.

**Tech Stack:** C++23, CMake ≥3.28, Drogon (vendored submodule `v1.9.13`), Tracy (new submodule), FGL CMake helpers, GoogleTest.

## Global Constraints

- **Zero-footprint when disabled:** without `IDHAN_ENABLE_TRACY`, every Tracy macro is a no-op and no coroutine code path changes. All instrumentation is guarded by `#ifdef TRACY_ENABLE`.
- **Single gate macro:** all `#ifdef` guards key on `TRACY_ENABLE` (propagated publicly by the `Tracy::TracyClient` target). The CMake option `IDHAN_ENABLE_TRACY` only triggers linking Tracy; do not gate C++ code on `IDHAN_ENABLE_TRACY`.
- **ODR consistency:** any target whose TUs instantiate `drogon::Task::promise_type` (both `drogon` and `IDHANServer`) must see the same `TRACY_ENABLE` state. Achieve this by linking `Tracy::TracyClient` PUBLIC on the `drogon` target when enabled.
- **Use existing type aliases** (`RecordID`, `TagID`, `SHA256`, etc.) — never raw ints. Match surrounding code style (Allman braces, spaced-out `< >` and `( )` as in the existing files).
- **Fiber name prefixes must be unique per coroutine family** (`idhan`, `job`, `drg`) so the three independent id counters cannot collide into a merged Tracy fiber.
- **Do not commit automatically.** Commit only the files listed in each task's commit step; leave submodule pointer commits for the user to review.
- **`drogon::when_all` stays on `drogon::Task`** — this plan does NOT change any coroutine return types.

---

## File Structure

**New files:**
- `dependencies/tracy/` — Tracy submodule (git submodule add).
- `dependencies/tracy-coro/include/idhan_tracy/CoroFiber.hpp` — shared, dependency-level coroutine-fiber helpers (`asAwaiter`, `FiberAwaiter`, `FiberInitialAwaiter`, `FiberFinalAwaiter`, fiber-id counter, thread-local tag, `makeFiberName`). Depends only on `<tracy/Tracy.hpp>` + std. Included by both IDHAN promises and the drogon patch, so it lives outside `IDHANServer/` to keep layering clean.
- `IDHANServer/src/profiling/tracy.hpp` — the app-facing zone wrapper header (`ZoneScoped`/`FrameMark`/`TracyFiber*` no-ops when disabled). Included by IDHAN app code that adds zones.
- `dependencies/patches/drogon-tracy-fibers.patch` — tracked patch of `drogon`'s `coroutine.h`.
- `docs/tracy.md` — how to build with Tracy, connect the GUI, and re-apply the drogon patch on bumps.
- `tests/src/profiling/fiberNameTest.cpp` — unit test for the pure name-builder.

**Modified files:**
- `CMakeLists.txt` (root) — `IDHAN_ENABLE_TRACY` option + Tracy subdirectory + tracy-coro include dir.
- `dependencies/Finddrogon.cmake` — link `Tracy::TracyClient` PUBLIC onto `drogon` when enabled.
- `IDHANServer/CMakeLists.txt` — link Tracy + define `IDHAN_ENABLE_TRACY`.
- `IDHANServer/src/threading/IDHANTask.hpp` — fiber hooks on both promise specializations.
- `IDHANServer/src/jobs/JobTaskPromise.hpp` / `.cpp` — fiber hooks.
- `IDHANServer/src/crypto/SHA256.cpp`, `core/search/SearchBuilder.cpp`, module call sites, `mime/MimeDatabase.cpp`, job runtime loop — `ZoneScoped` / `FrameMark`.
- `IDHANServer/src/ServerContext.cpp` — thread-local request tag in pre/post routing advice.
- `tests/CMakeLists.txt` — register the new test (only if tests glob is manual; verify).

---

## Task 1: Vendor Tracy, build gating, and the zone wrapper header

**Files:**
- Create: `dependencies/tracy/` (submodule)
- Modify: `CMakeLists.txt` (root)
- Modify: `IDHANServer/CMakeLists.txt`
- Create: `IDHANServer/src/profiling/tracy.hpp`
- Modify: `IDHANServer/src/ServerContext.cpp` (one proof-of-link zone)

**Interfaces:**
- Produces: the `TRACY_ENABLE` macro (public, from `Tracy::TracyClient`) and `IDHAN_ENABLE_TRACY` (on `IDHANServer`); the `profiling/tracy.hpp` header providing `ZoneScoped`, `ZoneScopedN(x)`, `ZoneScopedNC(x,c)`, `ZoneText(x,n)`, `ZoneName(x,n)`, `FrameMark`, `FrameMarkNamed(x)`, `TracyFiberEnter(x)`, `TracyFiberLeave`, `TracyMessageL(x)` (real when enabled, no-op when not).

- [ ] **Step 1: Add the Tracy submodule**

```bash
cd /home/kj16609/Desktop/Projects/cxx/IDHAN
git submodule add https://github.com/wolfpld/tracy.git dependencies/tracy
cd dependencies/tracy && git checkout v0.11.1 && cd ../..
```

Expected: `dependencies/tracy` populated; `.gitmodules` gains a `tracy` entry.

- [ ] **Step 2: Add the CMake option and Tracy subdirectory (root `CMakeLists.txt`)**

Add next to the other `option(...)` lines (after `option(IDHAN_ENABLE_TRACE ...)`):

```cmake
option(IDHAN_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)
```

Then, immediately before `add_subdirectory(IDHAN)`, add:

```cmake
if (IDHAN_ENABLE_TRACY)
	# Tracy client options must be set before its add_subdirectory so they bake into the target.
	set(TRACY_ENABLE ON CACHE BOOL "" FORCE)      # defines TRACY_ENABLE PUBLIC on Tracy::TracyClient
	set(TRACY_FIBERS ON CACHE BOOL "" FORCE)      # enables fiber (coroutine) support
	set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)   # only collect once a profiler connects
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tracy)
	# Dependency-level shared coroutine-fiber helpers, included by both IDHAN promises and the
	# drogon patch. Exposed as an INTERFACE target so its include dir propagates cleanly.
	add_library(idhan_tracy_coro INTERFACE)
	target_include_directories(idhan_tracy_coro INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tracy-coro/include)
	target_link_libraries(idhan_tracy_coro INTERFACE Tracy::TracyClient)
endif ()
```

- [ ] **Step 3: Link Tracy into `IDHANServer` (`IDHANServer/CMakeLists.txt`)**

After the existing `target_link_libraries(...)` block, add:

```cmake
if (IDHAN_ENABLE_TRACY)
	target_link_libraries(IDHANServer PUBLIC Tracy::TracyClient idhan_tracy_coro)
	target_compile_definitions(IDHANServer PUBLIC IDHAN_ENABLE_TRACY)
endif ()
```

- [ ] **Step 4: Create the zone wrapper header `IDHANServer/src/profiling/tracy.hpp`**

```cpp
//
// Tracy profiler wrapper. Include this (never <tracy/Tracy.hpp> directly) so that
// builds without IDHAN_ENABLE_TRACY compile every macro to a no-op.
//
#pragma once

#ifdef TRACY_ENABLE
	#include <tracy/Tracy.hpp>
#else
	// No-op fallbacks — keep #ifdef out of call sites.
	#define ZoneScoped
	#define ZoneScopedN( name )
	#define ZoneScopedNC( name, color )
	#define ZoneText( txt, size )
	#define ZoneName( txt, size )
	#define FrameMark
	#define FrameMarkNamed( name )
	#define TracyFiberEnter( fiber )
	#define TracyFiberLeave
	#define TracyMessageL( msg )
	#define TracyMessage( msg, size )
#endif
```

- [ ] **Step 5: Add one proof-of-link zone in `ServerContext::run` (`IDHANServer/src/ServerContext.cpp`)**

Add the include near the other project includes at the top of the file:

```cpp
#include "profiling/tracy.hpp"
```

Then add `ZoneScoped;` as the first statement inside `ServerContext::run()`:

```cpp
void ServerContext::run()
{
	ZoneScoped;
	log::info( "Starting runtime" );
```

- [ ] **Step 6: Verify the DISABLED build is unchanged (default)**

Run: `cmake --build build/debug --target IDHANServer 2>&1 | tail -5`
Expected: builds successfully; `profiling/tracy.hpp` macros are no-ops (no Tracy symbols linked).

- [ ] **Step 7: Verify the ENABLED build configures, compiles, and links**

Run:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DIDHAN_ENABLE_TRACY=ON -B build/tracy
cmake --build build/tracy --target IDHANServer -j"$(nproc)" 2>&1 | tail -15
```
Expected: configure pulls in Tracy; `IDHANServer` links `Tracy::TracyClient` with no undefined-symbol errors. (This proves the wrapper header + link wiring before any coroutine work.)

- [ ] **Step 8: Commit**

```bash
git add .gitmodules dependencies/tracy CMakeLists.txt IDHANServer/CMakeLists.txt \
	IDHANServer/src/profiling/tracy.hpp IDHANServer/src/ServerContext.cpp
git commit -m "build: vendor Tracy behind IDHAN_ENABLE_TRACY with no-op wrapper header"
```

---

## Task 2: Shared coroutine-fiber helpers + name-builder unit test

**Files:**
- Create: `dependencies/tracy-coro/include/idhan_tracy/CoroFiber.hpp`
- Create: `tests/src/profiling/fiberNameTest.cpp`
- Modify: `tests/CMakeLists.txt` (only if the test suite does not glob; verify first)

**Interfaces:**
- Produces (namespace `idhan::tracy_coro`):
  - `std::uint64_t nextFiberId()` — always defined; atomically increments a process-global counter.
  - `const char*& currentFiberTag()` — always defined; thread-local best-effort readable tag (default `nullptr`).
  - `std::string makeFiberName( const char* prefix )` — always defined; `"<prefix> #<id>"`, or `"<prefix> <tag> #<id>"` when a tag is set.
  - `template<typename A> decltype(auto) asAwaiter( A&& )` — `#ifdef TRACY_ENABLE`; normalizes any awaitable to its awaiter (handles member/free `operator co_await` and direct-awaiter types).
  - `template<typename Awaitable> struct FiberAwaiter` — `#ifdef TRACY_ENABLE`; wraps an awaiter, brackets suspension with `TracyFiberLeave`/`TracyFiberEnter(name)`.
  - `struct FiberInitialAwaiter { const char* name; }` — `#ifdef TRACY_ENABLE`; `await_resume()` calls `TracyFiberEnter(name)`.
  - `template<typename Inner> struct FiberFinalAwaiter` — `#ifdef TRACY_ENABLE`; wraps an inner final awaiter, calls `TracyFiberLeave` before delegating `await_suspend`.

- [ ] **Step 1: Write the failing name-builder test `tests/src/profiling/fiberNameTest.cpp`**

```cpp
#include <gtest/gtest.h>

#include "idhan_tracy/CoroFiber.hpp"

using namespace idhan::tracy_coro;

TEST( FiberName, IdsAreUniqueAndMonotonic )
{
	const auto a { nextFiberId() };
	const auto b { nextFiberId() };
	EXPECT_NE( a, b );
	EXPECT_LT( a, b );
}

TEST( FiberName, PrefixWithoutTag )
{
	currentFiberTag() = nullptr;
	const auto name { makeFiberName( "idhan" ) };
	EXPECT_TRUE( name.starts_with( "idhan #" ) );
}

TEST( FiberName, PrefixWithTag )
{
	currentFiberTag() = "GET /search";
	const auto name { makeFiberName( "idhan" ) };
	EXPECT_NE( name.find( "GET /search" ), std::string::npos );
	EXPECT_TRUE( name.starts_with( "idhan GET /search #" ) );
	currentFiberTag() = nullptr; // reset for other tests
}
```

- [ ] **Step 2: Register the test if needed, then run it to verify it FAILS (header missing)**

First check whether the test suite globs sources:
Run: `grep -nE "GLOB|AddFGL|target_sources" tests/CMakeLists.txt | head`
- If it globs `src/**`, no edit needed.
- Otherwise add `tests/src/profiling/fiberNameTest.cpp` to the test target's sources, and add the tracy-coro include dir to the test target:
```cmake
target_include_directories(IDHANTests PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/tracy-coro/include)
```

Run: `cmake --build build/debug --target IDHANTests 2>&1 | tail -5`
Expected: FAIL — `idhan_tracy/CoroFiber.hpp` not found.

- [ ] **Step 3: Create `dependencies/tracy-coro/include/idhan_tracy/CoroFiber.hpp`**

```cpp
//
// Dependency-level coroutine-fiber helpers shared by IDHAN's coroutine promises and the
// vendored-drogon Tracy patch. The pure name helpers are always defined (cheap, no Tracy dep)
// so they remain unit-testable; the awaiter machinery is gated on TRACY_ENABLE.
//
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace idhan::tracy_coro
{

//! Process-global monotonically increasing fiber id source.
inline std::uint64_t nextFiberId()
{
	static std::atomic< std::uint64_t > counter { 0 };
	return counter.fetch_add( 1, std::memory_order_relaxed );
}

//! Best-effort readable tag for the coroutine created on this thread (e.g. "GET /search").
//! Never owned here — the setter guarantees lifetime for the duration it is set.
inline const char*& currentFiberTag()
{
	static thread_local const char* tag { nullptr };
	return tag;
}

//! Build a unique, persistent-per-instance fiber name. \p prefix distinguishes coroutine families
//! ("idhan", "job", "drg") so the independent id counters cannot merge into one Tracy fiber.
inline std::string makeFiberName( const char* prefix )
{
	const auto id { nextFiberId() };
	if ( const char* tag = currentFiberTag() )
		return std::string( prefix ) + ' ' + tag + " #" + std::to_string( id );
	return std::string( prefix ) + " #" + std::to_string( id );
}

#ifdef TRACY_ENABLE

} // namespace idhan::tracy_coro

	#include <tracy/Tracy.hpp>

	#include <coroutine>
	#include <utility>

namespace idhan::tracy_coro
{

//! Normalize any awaitable to its awaiter: member operator co_await, free operator co_await, or the
//! object itself when it is already an awaiter (has await_ready).
template < typename A >
decltype( auto ) asAwaiter( A&& awaitable )
{
	if constexpr ( requires { std::forward< A >( awaitable ).operator co_await(); } )
		return std::forward< A >( awaitable ).operator co_await();
	else if constexpr ( requires { operator co_await( std::forward< A >( awaitable ) ); } )
		return operator co_await( std::forward< A >( awaitable ) );
	else
		return std::forward< A >( awaitable );
}

//! Wraps an awaited expression, bracketing the suspension point with fiber leave/enter so the
//! coroutine's Tracy zone stack is saved on suspend and restored on resume — even across thread hops.
//! \tparam Awaitable is `T&` for lvalue awaitables (not owned) or `T` for rvalues (owned), via the
//!         forwarding-reference deduction in await_transform. This is required because some call sites
//!         co_await a named, move-only awaitable lvalue (e.g. when_all stored in a local).
template < typename Awaitable >
struct FiberAwaiter
{
	Awaitable m_awaitable;
	const char* m_name;
	decltype( asAwaiter( std::declval< Awaitable& >() ) ) m_awaiter;

	FiberAwaiter( Awaitable&& awaitable, const char* name ) :
	  m_awaitable( std::forward< Awaitable >( awaitable ) ),
	  m_name( name ),
	  m_awaiter( asAwaiter( m_awaitable ) )
	{}

	bool await_ready() { return m_awaiter.await_ready(); }

	template < typename Handle >
	auto await_suspend( Handle handle )
	{
		TracyFiberLeave;
		return m_awaiter.await_suspend( handle );
	}

	decltype( auto ) await_resume()
	{
		TracyFiberEnter( m_name );
		return m_awaiter.await_resume();
	}
};

//! Initial awaiter: suspends (lazy body), and on first resume enters the fiber so the body's own
//! zones attribute to this coroutine's fiber.
struct FiberInitialAwaiter
{
	const char* m_name;

	bool await_ready() const noexcept { return false; }

	void await_suspend( std::coroutine_handle<> ) const noexcept {}

	void await_resume() const noexcept { TracyFiberEnter( m_name ); }
};

//! Final awaiter wrapping an inner final awaiter (e.g. drogon::final_awaiter). Leaves the fiber
//! before the symmetric transfer back to the continuation.
template < typename Inner >
struct FiberFinalAwaiter
{
	Inner m_inner;

	bool await_ready() noexcept { return m_inner.await_ready(); }

	template < typename Handle >
	auto await_suspend( Handle handle ) noexcept
	{
		TracyFiberLeave;
		return m_inner.await_suspend( handle );
	}

	void await_resume() noexcept { m_inner.await_resume(); }
};

#endif // TRACY_ENABLE

} // namespace idhan::tracy_coro
```

- [ ] **Step 4: Run the name-builder test to verify it PASSES**

Run: `cmake --build build/debug --target IDHANTests && ./build/bin/IDHANTests --gtest_filter='FiberName.*' --use_stdout`
Expected: PASS (3 tests). This build is Tracy-disabled, exercising the always-defined pure helpers.

- [ ] **Step 5: Commit**

```bash
git add dependencies/tracy-coro tests/src/profiling/fiberNameTest.cpp tests/CMakeLists.txt
git commit -m "feat: add shared coroutine-fiber tracing helpers + name-builder test"
```

---

## Task 3: Fiber hooks on IDHANTask (both specializations)

**Files:**
- Modify: `IDHANServer/src/threading/IDHANTask.hpp`

**Interfaces:**
- Consumes: `idhan::tracy_coro::{FiberAwaiter, FiberInitialAwaiter, FiberFinalAwaiter, makeFiberName}` from Task 2.
- Produces: `IDHANTask<T>` and `IDHANTask<void>` whose bodies and suspension points are fiber-bracketed when `TRACY_ENABLE`; identical to current code when not.

- [ ] **Step 1: Add the include (guarded) at the top of `IDHANTask.hpp`**

After the existing includes:

```cpp
#ifdef TRACY_ENABLE
	#include "idhan_tracy/CoroFiber.hpp"
#endif
```

- [ ] **Step 2: Add fiber state + hooks to `IDHANTask<T>::promise_type`**

Inside `struct promise_type` (the `T` specialization), add a guarded name member and replace `initial_suspend`/`final_suspend`, and add `await_transform`. Concretely:

Add member (alongside `value`, `exception_`, `continuation_`):
```cpp
#ifdef TRACY_ENABLE
		std::string m_fiber_name { idhan::tracy_coro::makeFiberName( "idhan" ) };
#endif
```

Replace:
```cpp
		static std::suspend_always initial_suspend() { return {}; }
```
with:
```cpp
#ifdef TRACY_ENABLE
		auto initial_suspend() noexcept { return idhan::tracy_coro::FiberInitialAwaiter { m_fiber_name.c_str() }; }
#else
		static std::suspend_always initial_suspend() { return {}; }
#endif
```

Replace:
```cpp
		static auto final_suspend() noexcept { return drogon::final_awaiter {}; }
```
with:
```cpp
#ifdef TRACY_ENABLE
		auto final_suspend() noexcept
		{
			return idhan::tracy_coro::FiberFinalAwaiter< drogon::final_awaiter > { drogon::final_awaiter {} };
		}
#else
		static auto final_suspend() noexcept { return drogon::final_awaiter {}; }
#endif
```

Add, inside the promise (e.g. after `setContinuation`):
```cpp
#ifdef TRACY_ENABLE
		template < typename Awaitable >
		auto await_transform( Awaitable&& awaitable )
		{
			return idhan::tracy_coro::FiberAwaiter< Awaitable > { std::forward< Awaitable >( awaitable ),
				                                                   m_fiber_name.c_str() };
		}
#endif
```

- [ ] **Step 3: Repeat the same four edits on `IDHANTask<void>::promise_type`**

Apply the identical member, `initial_suspend`, `final_suspend`, and `await_transform` changes to the `void` specialization's `promise_type`.

- [ ] **Step 4: Verify the DISABLED build is unchanged**

Run: `cmake --build build/debug --target IDHANServer 2>&1 | tail -5`
Expected: builds; the `#else` branches are byte-for-byte the original code.

- [ ] **Step 5: Verify the ENABLED build compiles (template correctness of await_transform)**

Run: `cmake --build build/tracy --target IDHANServer -j"$(nproc)" 2>&1 | tail -20`
Expected: compiles. This exercises `FiberAwaiter`/`await_transform` against every awaitable IDHANTask bodies actually use (`execSqlCoro`, nested `IDHANTask`/`ExpectedTask`, `when_all`, `sleepCoro`). If a specific awaitable fails to normalize, that is sharp-edge #3 from the spec — fix `asAwaiter` to cover that shape before proceeding.

- [ ] **Step 6: Commit**

```bash
git add IDHANServer/src/threading/IDHANTask.hpp
git commit -m "feat: fiber-instrument IDHANTask promises for Tracy coroutine timelines"
```

---

## Task 4: Fiber hooks on JobTaskPromise

**Files:**
- Modify: `IDHANServer/src/jobs/JobTaskPromise.hpp`
- Modify: `IDHANServer/src/jobs/JobTaskPromise.cpp`

**Interfaces:**
- Consumes: `idhan::tracy_coro::{FiberInitialAwaiter, FiberFinalAwaiter, FiberAwaiter, makeFiberName}`.
- Produces: `JobTask` bodies fiber-bracketed when enabled. JobTask is top-level (plain `std::suspend_always`, no continuation), so its final awaiter wraps `std::suspend_always`, not `drogon::final_awaiter`.

- [ ] **Step 1: Declare the hooks in `JobTaskPromise.hpp`**

Add the guarded include after the existing includes:
```cpp
#ifdef TRACY_ENABLE
	#include "idhan_tracy/CoroFiber.hpp"

	#include <string>
#endif
```

Add a name member and change the `initial_suspend`/`final_suspend` return types. Replace:
```cpp
	std::suspend_always initial_suspend();

	std::suspend_always final_suspend() noexcept;
```
with:
```cpp
#ifdef TRACY_ENABLE
	std::string m_fiber_name { idhan::tracy_coro::makeFiberName( "job" ) };

	idhan::tracy_coro::FiberInitialAwaiter initial_suspend();

	idhan::tracy_coro::FiberFinalAwaiter< std::suspend_always > final_suspend() noexcept;

	template < typename Awaitable >
	auto await_transform( Awaitable&& awaitable )
	{
		return idhan::tracy_coro::FiberAwaiter< Awaitable > { std::forward< Awaitable >( awaitable ),
			                                                   m_fiber_name.c_str() };
	}
#else
	std::suspend_always initial_suspend();

	std::suspend_always final_suspend() noexcept;
#endif
```

- [ ] **Step 2: Update the definitions in `JobTaskPromise.cpp`**

Replace the `initial_suspend` definition:
```cpp
std::suspend_always JobTaskPromise::initial_suspend()
{
	return {};
}
```
with:
```cpp
#ifdef TRACY_ENABLE
idhan::tracy_coro::FiberInitialAwaiter JobTaskPromise::initial_suspend()
{
	return { m_fiber_name.c_str() };
}
#else
std::suspend_always JobTaskPromise::initial_suspend()
{
	return {};
}
#endif
```

Replace the `final_suspend` definition:
```cpp
std::suspend_always JobTaskPromise::final_suspend() noexcept
{
	m_status->m_completion_time = std::chrono::steady_clock::now();
	m_status->m_done = true;
	return {};
}
```
with:
```cpp
#ifdef TRACY_ENABLE
idhan::tracy_coro::FiberFinalAwaiter< std::suspend_always > JobTaskPromise::final_suspend() noexcept
{
	m_status->m_completion_time = std::chrono::steady_clock::now();
	m_status->m_done = true;
	return { std::suspend_always {} };
}
#else
std::suspend_always JobTaskPromise::final_suspend() noexcept
{
	m_status->m_completion_time = std::chrono::steady_clock::now();
	m_status->m_done = true;
	return {};
}
#endif
```

- [ ] **Step 3: Verify both builds**

Run:
```bash
cmake --build build/debug --target IDHANServer 2>&1 | tail -5
cmake --build build/tracy  --target IDHANServer -j"$(nproc)" 2>&1 | tail -10
```
Expected: both succeed.

- [ ] **Step 4: Commit**

```bash
git add IDHANServer/src/jobs/JobTaskPromise.hpp IDHANServer/src/jobs/JobTaskPromise.cpp
git commit -m "feat: fiber-instrument JobTask promise for Tracy coroutine timelines"
```

---

## Task 5: Patch vendored drogon + ODR build wiring

**Files:**
- Modify: `dependencies/drogon/lib/inc/drogon/utils/coroutine.h` (both `Task<T>` and `Task<void>` promise_types)
- Create: `dependencies/patches/drogon-tracy-fibers.patch`
- Modify: `dependencies/Finddrogon.cmake`
- Create: `docs/tracy.md`

**Interfaces:**
- Consumes: `idhan::tracy_coro::*` (drogon TUs get the include dir + `TRACY_ENABLE` via the Tracy link added here).
- Produces: `drogon::Task<T>`/`Task<void>` fiber-bracketed when enabled; a tracked patch reproducing the edit; drogon and IDHANServer share `TRACY_ENABLE` (no ODR split).

- [ ] **Step 1: Wire Tracy PUBLIC onto the `drogon` target (`dependencies/Finddrogon.cmake`)**

Inside the `if(NOT TARGET drogon)` block, after the `add_subdirectory(... drogon ...)` line and the `-w` loop, add:

```cmake
	# Tracy: link PUBLIC so drogon's own TUs and every consumer instantiate the SAME
	# Task::promise_type layout (TRACY_ENABLE consistent) — otherwise ODR violation. Also gives
	# drogon's coroutine.h the tracy-coro include dir + <tracy/Tracy.hpp>.
	if (IDHAN_ENABLE_TRACY AND TARGET Tracy::TracyClient)
		target_link_libraries(drogon PUBLIC Tracy::TracyClient idhan_tracy_coro)
	endif ()
```

Note: `Finddrogon.cmake` is included from `IDHANServer/CMakeLists.txt` (`find_package(drogon REQUIRED)`), which runs after the root added Tracy — so the `Tracy::TracyClient`/`idhan_tracy_coro` targets already exist.

- [ ] **Step 2: Patch `Task<T>::promise_type` in `coroutine.h`**

Add the guarded include once near the top of `coroutine.h` (after its existing includes):
```cpp
#ifdef TRACY_ENABLE
#include <idhan_tracy/CoroFiber.hpp>
#endif
```

In `Task<T>::promise_type`, add a name member with the other members:
```cpp
#ifdef TRACY_ENABLE
        std::string tracyFiberName_{idhan::tracy_coro::makeFiberName("drg")};
#endif
```

Replace `initial_suspend`:
```cpp
        std::suspend_always initial_suspend()
        {
            return {};
        }
```
with:
```cpp
#ifdef TRACY_ENABLE
        auto initial_suspend() noexcept
        {
            return idhan::tracy_coro::FiberInitialAwaiter{tracyFiberName_.c_str()};
        }
#else
        std::suspend_always initial_suspend()
        {
            return {};
        }
#endif
```

Replace `final_suspend`:
```cpp
        auto final_suspend() noexcept
        {
            return final_awaiter{};
        }
```
with:
```cpp
#ifdef TRACY_ENABLE
        auto final_suspend() noexcept
        {
            return idhan::tracy_coro::FiberFinalAwaiter<final_awaiter>{final_awaiter{}};
        }
#else
        auto final_suspend() noexcept
        {
            return final_awaiter{};
        }
#endif
```

Add `await_transform` after `setContinuation`:
```cpp
#ifdef TRACY_ENABLE
        template <typename Awaitable>
        auto await_transform(Awaitable &&awaitable)
        {
            return idhan::tracy_coro::FiberAwaiter<Awaitable>{
                std::forward<Awaitable>(awaitable), tracyFiberName_.c_str()};
        }
#endif
```

- [ ] **Step 3: Apply the identical four edits to `Task<void>::promise_type`**

Same member, `initial_suspend`, `final_suspend`, and `await_transform` in the `Task<void>` specialization's `promise_type`.

- [ ] **Step 4: Verify the ENABLED build compiles across the whole server**

Run: `cmake --build build/tracy -j"$(nproc)" 2>&1 | tail -25`
Expected: full `IDHANServer` build succeeds. This is the broadest `await_transform` exercise — all ~354 `drogon::Task` bodies, including handlers and the `when_all` sites (`api/record/tags/addTags.cpp`, `hyapi/getMetadataInfo.cpp:235`). If a `co_await` fails to compile, extend `asAwaiter` (sharp edge #3), rebuild, and re-run before continuing.

- [ ] **Step 5: Verify the DISABLED build still matches upstream drogon behavior**

Run: `cmake --build build/debug -j"$(nproc)" 2>&1 | tail -5`
Expected: succeeds; every drogon edit is inside `#ifdef TRACY_ENABLE`, so the disabled path is identical to upstream.

- [ ] **Step 6: Capture the patch as a tracked file**

```bash
cd /home/kj16609/Desktop/Projects/cxx/IDHAN
mkdir -p dependencies/patches
git -C dependencies/drogon diff -- lib/inc/drogon/utils/coroutine.h > dependencies/patches/drogon-tracy-fibers.patch
```
Expected: `dependencies/patches/drogon-tracy-fibers.patch` contains only the guarded `coroutine.h` hunks.

- [ ] **Step 7: Document build + patch re-application (`docs/tracy.md`)**

Write `docs/tracy.md` covering: enabling the build (`-DIDHAN_ENABLE_TRACY=ON`), that it is scenario-profiling (attach the Tracy GUI, run one operation, capture), the `TRACY_ON_DEMAND` behavior, and the drogon-bump workflow:
```markdown
# Profiling IDHAN with Tracy

Configure: `cmake -DIDHAN_ENABLE_TRACY=ON -B build/tracy && cmake --build build/tracy --target IDHANServer`

Run the server, then open the Tracy profiler GUI and connect (on-demand: collection starts on connect).
Intended for *scenario* profiling — trace one search / one import, capture, analyze — not 24/7.

## Re-applying the drogon coroutine patch after a submodule bump

The `drogon::Task` fiber hooks live in `dependencies/patches/drogon-tracy-fibers.patch`.
After bumping the drogon submodule:

    git -C dependencies/drogon apply ../../dependencies/patches/drogon-tracy-fibers.patch

If it fails to apply, re-create the four guarded edits by hand (see Task 5 of the plan) and
regenerate the patch with `git -C dependencies/drogon diff -- lib/inc/drogon/utils/coroutine.h`.
```

- [ ] **Step 8: Commit**

```bash
git add dependencies/patches/drogon-tracy-fibers.patch dependencies/Finddrogon.cmake docs/tracy.md
git commit -m "feat: patch drogon Task promises for Tracy fibers + ODR-safe build wiring"
```
Note: the `dependencies/drogon` submodule working-tree edit is intentionally left uncommitted at the superproject level (the patch file is the tracked source of truth). Mention this to the user.

---

## Task 6: Synchronous zone instrumentation on CPU hot paths

**Files:**
- Modify: `IDHANServer/src/crypto/SHA256.cpp`
- Modify: `IDHANServer/src/core/search/SearchBuilder.cpp`
- Modify: `IDHANServer/src/mime/MimeDatabase.cpp`
- Modify: module thumbnail/generate call sites (see Step 4)
- Modify: the job runtime loop (`FrameMark`)

**Interfaces:**
- Consumes: `profiling/tracy.hpp` macros.
- Produces: named zones nested inside the fibers from Tasks 3–5.

For each file below, add `#include "profiling/tracy.hpp"` with the other project includes, then add the zone as the first statement of the named function.

- [ ] **Step 1: Zone the SHA256 hashing leaf (`crypto/SHA256.cpp`)**

In `SHA256::hash(const std::byte* data, const std::size_t size)` (line ~148), first statement:
```cpp
	ZoneScopedN( "SHA256::hash" );
```
And in `SHA256::hashCoro(FileIOUring io_uring)` (line ~153), add `ZoneScopedN( "SHA256::hashCoro" );` as the first statement (the zone spans the coroutine's synchronous compute; it is safe because Task 3/5 fibers save/restore the stack across its awaits).

- [ ] **Step 2: Zone SearchBuilder (`core/search/SearchBuilder.cpp`)**

`SearchBuilder::construct(...)` (line ~495) first statement:
```cpp
	ZoneScopedN( "SearchBuilder::construct" );
```
`SearchBuilder::query(...)` (line ~591) first statement:
```cpp
	ZoneScopedN( "SearchBuilder::query" );
```

- [ ] **Step 3: Zone mime scanning (`mime/MimeDatabase.cpp`)**

In `MimeDatabase::scan(Cursor cursor)` (line ~31, the base implementation) first statement:
```cpp
	ZoneScopedN( "MimeDatabase::scan" );
```

- [ ] **Step 4: Zone the module calls**

Find the server-side call sites that invoke `ModuleBase::thumbnail(...)` and `ModuleBase::generate(...)` / `GeneratorModule::generate(...)`:
Run: `grep -rnE "->thumbnail\(|->generate\(" IDHANServer/src --include='*.cpp'`
At each call site, wrap with a named zone in the enclosing function, e.g. immediately before the call:
```cpp
	ZoneScopedN( "module::thumbnail" );
```
(Use `"module::generate"` for generate call sites.) Add the `profiling/tracy.hpp` include to each touched file.

- [ ] **Step 5: Add a frame mark to the job runtime loop**

Run: `grep -rnE "class JobRuntime|::run\(|while|processJobs|queueJob" IDHANServer/src/jobs/*.cpp | head`
Locate the job runtime's per-iteration loop body and add, at the top of each iteration:
```cpp
	FrameMarkNamed( "jobs" );
```
Add the `profiling/tracy.hpp` include to that file.

- [ ] **Step 6: Verify both builds**

Run:
```bash
cmake --build build/debug --target IDHANServer 2>&1 | tail -5
cmake --build build/tracy  --target IDHANServer -j"$(nproc)" 2>&1 | tail -10
```
Expected: both succeed.

- [ ] **Step 7: Commit**

```bash
git add IDHANServer/src/crypto/SHA256.cpp IDHANServer/src/core/search/SearchBuilder.cpp \
	IDHANServer/src/mime/MimeDatabase.cpp IDHANServer/src/jobs
# plus any module call-site files touched in Step 4
git commit -m "feat: add Tracy zones to SHA256, SearchBuilder, mime, modules, job loop"
```

---

## Task 7: Request/job correlation (readable fiber names)

**Files:**
- Modify: `IDHANServer/src/ServerContext.cpp` (`setupCORSSupport`)

**Interfaces:**
- Consumes: `idhan::tracy_coro::currentFiberTag()`.
- Produces: handler-family fibers named with the request line (`"drg GET /search #4213"`), best-effort.

- [ ] **Step 1: Set the thread-local tag in the pre-routing advice**

In `ServerContext::setupCORSSupport`, add the guarded include at the top of `ServerContext.cpp`:
```cpp
#ifdef TRACY_ENABLE
	#include "idhan_tracy/CoroFiber.hpp"
#endif
```
Inside the existing `registerPreRoutingAdvice` lambda, before `pass();`, add:
```cpp
#ifdef TRACY_ENABLE
				// Best-effort: names the handler coroutine (constructed on this thread right after
				// routing) with the request line. Stored per-thread; the string is owned by the request.
				static thread_local std::string tag;
				tag = request->getMethodString() + std::string( " " ) + request->getPath();
				idhan::tracy_coro::currentFiberTag() = tag.c_str();
#endif
```

- [ ] **Step 2: Clear the tag in the post-handling advice**

Inside the existing `registerPostHandlingAdvice` lambda, add:
```cpp
#ifdef TRACY_ENABLE
				idhan::tracy_coro::currentFiberTag() = nullptr;
#endif
```

- [ ] **Step 3: Verify both builds**

Run:
```bash
cmake --build build/debug --target IDHANServer 2>&1 | tail -5
cmake --build build/tracy  --target IDHANServer -j"$(nproc)" 2>&1 | tail -10
```
Expected: both succeed.

- [ ] **Step 4: Commit**

```bash
git add IDHANServer/src/ServerContext.cpp
git commit -m "feat: tag Tracy fibers with the request line for readable timelines"
```

---

## Task 8: End-to-end verification (manual, with the Tracy GUI)

**Files:** none (verification only).

This task has no unit test — Tracy has no programmatic readback. Run these checks and record results. Because this repo's convention is that the developer runs the server (not the agent), hand these steps to the user if executing via subagent.

- [ ] **Step 1: Build the enabled server and launch it**

```bash
cmake --build build/tracy --target IDHANServer -j"$(nproc)"
./build/tracy/bin/IDHANServer --use_stdout
```
Expected: server starts normally (on-demand: no profiling overhead until a GUI connects).

- [ ] **Step 2: Connect the Tracy profiler GUI and confirm live data**

Open the Tracy GUI (`tracy-profiler`), connect to the server host. Expected: connection succeeds; frames/threads appear.

- [ ] **Step 3: Exercise a representative flow and verify a single request is one continuous fiber**

Issue one search request and one import job. In the GUI:
- Confirm a fiber track named like `drg GET /search #N` exists.
- Confirm that fiber spans its DB `co_await`s **across thread hops** as one continuous timeline (not fragmented per thread).
- Confirm synchronous zones (`SearchBuilder::construct`, `SHA256::hash`) appear nested within the fibers.

Expected: timelines are continuous and zones nest correctly. If a fiber fragments at a suspension, revisit the `await_transform`/final-awaiter bracketing (Tasks 3/5).

- [ ] **Step 4: Fiber-name lifetime check (sharp edge #1)**

Drive a high request count (e.g. `hey`/`ab` a few thousand requests at the server) with the GUI attached. Expected: no crash, no garbage/corrupt fiber names in the GUI. If names corrupt, the promise-owned `std::string` is being read after frame destruction → intern names into a bounded pool keyed by id (documented fallback) and re-verify.

- [ ] **Step 5: Record results**

Note the outcomes of Steps 3–4 in `docs/tracy.md` (a short "Verified on <date>" line) and report to the user. No commit required unless `docs/tracy.md` is updated.

---

## Notes for the executor

- **Build/run is the developer's job in this repo.** When a step says "run", request that the user run it and report output, rather than building/launching the server yourself.
- **Order matters:** Task 5 (drogon patch) depends on Task 2's helpers being present and on Task 1's Tracy wiring. Do not reorder.
- **If `asAwaiter` fails to cover an awaitable** encountered in Task 3 or 5, that is the expected place to extend it — add the missing `if constexpr` branch, rebuild, re-run. This is sharp edge #3 from the spec and the single most likely source of enabled-build compile errors.
