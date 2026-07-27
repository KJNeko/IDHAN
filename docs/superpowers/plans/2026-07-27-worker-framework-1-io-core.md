# Worker Framework, Plan 1: io_uring Core Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:
> executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sever the io_uring layer's dependency on drogon and trantor, then move it out of `IDHANServer` into the shared
`IDHAN` library so the Monitor and Worker processes (Plans 2 and 3) can link the same transport core.

**Architecture:** Introduce three new pieces in `IDHAN` under `idhan::coro`: a neutral lazy `Task<T>` coroutine type, a
`Resumer` interface that decides where a completed io operation resumes its awaiting coroutine, and a `RunLoop` (plus
`RunLoopResumer`) that gives a trantor-free process somewhere to resume. The awaiters stop calling
`trantor::EventLoop::getEventLoopOfCurrentThread()` and instead capture `coro::currentResumer()` at suspend time; the
server installs a provider that hands back a per-thread `TrantorResumer`, preserving today's loop-affinity behaviour
exactly. Once the layer is drogon-free in place, it is moved wholesale to `IDHAN/include/io/` and `IDHAN/src/io/`.

**Tech Stack:** C++23, liburing (Linux), Windows IoRing / IOCP backends, spdlog, GoogleTest, CMake with `AddFGLLibrary`.

**Spec:** `docs/superpowers/specs/2026-07-26-worker-framework-design.md` (sections 3.1 and 10)

**Plan sequence:** This is plan 1 of 3.

1. **This plan** — io_uring core extraction into `IDHAN`, drogon/trantor decoupled.
2. Transport — frame header, `Channel`, `sendmsg`/`recvmsg`, `Blob` (memfd + `SCM_RIGHTS`), socketpair round-trip tests.
3. Processes — `IDHANMonitor`, `IDHANWorker`, supervision (timeout, crash, memory watermark), config, server client.

## Global Constraints

- **Do not build or run anything yourself.** Ask the user to run each build/test command and report the result back.
  This applies to every "verify" step below.
- **No emojis anywhere**, including commit messages and code comments.
- **One commit per task.** Never `git add -A` or `git add .`. Every commit stages its exact file paths. The `git add`
  lines below are already written that way — use them verbatim.
- **Branch:** the checkout at planning time was `feature/tracy-profiling`. Per `CLAUDE.md`, feature branches merge into
  `dev`, not `master`. Confirm with the user which branch to work on before Task 1; do not branch unasked.
- **`IDHAN` is an `OBJECT` library.** Its objects are copied into every consumer — `IDHANServer`, `IDHANClient`
  (SHARED), `IDHANPremadeModules` (MODULE, `dlopen`'d `RTLD_GLOBAL`), `HydrusImporter`, `IDHANTests`. This was an
  explicit decision (spec S3.1 says the io backend moves into `IDHAN`). It means every file-scope global added to
  `IDHAN` exists once per consumer. Two such globals arrive in this plan: `g_provider` in `coro/Resumer.cpp` and
  `g_linux_instance` in the moved `io/linux/IOUringLinux.cpp`. **Both are inert unless `IOUring::init()` is called**,
  and only the server (and later the Monitor and Worker) calls it. Do not add any global to `IDHAN` that is live
  without an explicit init call, and do not make modules call `IOUring::init()`.
- **`co_await` is generic.** Neither `drogon::Task`'s promise nor `IDHANTask`'s promise defines `await_transform`, so a
  `drogon::Task` or `IDHANTask` body can `co_await` a `coro::Task<T>` with no change at the call site. Task 6 relies on
  this: it changes the io layer's return types and touches **zero** consumer files.
- **Every existing io call site is a plain `co_await`.** None of the nine consumers stores an io `Task` in a variable,
  puts one in a container, or passes one to `drogon::when_all`. Verified at planning time across `ServerContext.cpp`,
  `api/cluster/scan.cpp`, `api/record/fetchThumbnail.cpp`, `crypto/SHA256.cpp`, `metadata/updateRecordMetadata.cpp`,
  `metadata/parseMetadata.cpp`, `mime/MimeDatabase.hpp`, `mime/Cursor.hpp`, `mime/Cursor.cpp`, `mime/FileInfo.cpp`.
- **Build command** (substitute another pre-configured tree from `build/` if the user prefers):
  `cmake --build build/debug --target IDHANServer`
- **Test build command:** `cmake -DBUILD_IDHAN_TESTS=ON -B build/debug && cmake --build build/debug --target IDHANTests`
- **Test run command:** `./build/bin/IDHANTests --gtest_filter="<Suite>.*" --use_stdout`
  Do **not** run bare `ctest` for these tasks: the rest of the suite needs a live PostgreSQL instance, and every test
  added by this plan is deliberately DB-free.

---

### Task 1: Neutral `coro::Task<T>` coroutine type

**Files:**

- Create: `IDHAN/include/coro/Task.hpp`
- Test: `tests/src/coro/task.cpp`

**Interfaces:**

- Consumes: nothing from earlier tasks.
- Produces: `idhan::coro::Task<T>` (default `T = void`), a lazy move-only coroutine return type with
  `operator co_await()`. Task 3 uses it in `runOnLoop`; Task 6 uses it as the return type of every `IOUring` and
  `FileIOUring` method; Task 9 uses it in a test coroutine.

**Why this is first:** everything downstream is typed in terms of it, and it is the one piece with no platform or
build-layout entanglement, so it can be written and tested on its own.

- [ ] **Step 1: Write the failing test**

Create `tests/src/coro/task.cpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//

#include <gtest/gtest.h>

#include <coroutine>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include "coro/Task.hpp"

namespace
{

idhan::coro::Task< int > answer()
{
	co_return 42;
}

idhan::coro::Task< int > doubled()
{
	const auto value { co_await answer() };
	co_return value * 2;
}

idhan::coro::Task< std::string > moveOnlyPayload()
{
	std::string big( 1024, 'x' );
	co_return big;
}

idhan::coro::Task< int > thrower()
{
	throw std::runtime_error( "boom" );
	co_return 0;
}

idhan::coro::Task< int > catcher()
{
	try
	{
		co_return co_await thrower();
	}
	catch ( const std::runtime_error& )
	{
		co_return -1;
	}
}

int g_side_effect { 0 };

idhan::coro::Task<> bumpSideEffect()
{
	g_side_effect = 1;
	co_return;
}

idhan::coro::Task<> awaitVoid()
{
	co_await bumpSideEffect();
	g_side_effect += 1;
	co_return;
}

// Drives a Task to completion from non-coroutine code. Only valid for a task that never suspends on
// anything asynchronous -- every task in this file completes synchronously once started. Eager
// (suspend_never at both ends), so the body runs on the calling thread and the frame self-destroys.
struct SyncDriver
{
	struct promise_type
	{
		SyncDriver get_return_object() const noexcept { return {}; }

		static std::suspend_never initial_suspend() noexcept { return {}; }

		static std::suspend_never final_suspend() noexcept { return {}; }

		void return_void() const noexcept {}

		void unhandled_exception() const { std::terminate(); }
	};
};

template < typename T >
SyncDriver driveInto( idhan::coro::Task< T > task, T* out )
{
	*out = co_await task;
}

SyncDriver driveVoid( idhan::coro::Task<> task )
{
	co_await task;
}

template < typename T >
T syncRun( idhan::coro::Task< T > task )
{
	T out {};
	driveInto< T >( std::move( task ), &out );
	return out;
}

} // namespace

TEST( CoroTask, returnsValue )
{
	EXPECT_EQ( syncRun( answer() ), 42 );
}

TEST( CoroTask, awaitsNestedTask )
{
	EXPECT_EQ( syncRun( doubled() ), 84 );
}

TEST( CoroTask, movesPayloadOut )
{
	EXPECT_EQ( syncRun( moveOnlyPayload() ).size(), 1024u );
}

TEST( CoroTask, propagatesExceptionThroughAwait )
{
	EXPECT_EQ( syncRun( catcher() ), -1 );
}

TEST( CoroTask, isLazy )
{
	g_side_effect = 0;
	{
		// Constructed but never awaited: initial_suspend is suspend_always, so the body must not run.
		auto task { bumpSideEffect() };
		EXPECT_EQ( g_side_effect, 0 );
	}
	EXPECT_EQ( g_side_effect, 0 );
}

TEST( CoroTask, voidSpecialisationRuns )
{
	g_side_effect = 0;

	driveVoid( awaitVoid() );

	EXPECT_EQ( g_side_effect, 2 );
}
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run:

```bash
cmake -DBUILD_IDHAN_TESTS=ON -B build/debug && cmake --build build/debug --target IDHANTests
```

Expected: **compile failure**, `fatal error: coro/Task.hpp: No such file or directory`.

- [ ] **Step 3: Create the header**

Create `IDHAN/include/coro/Task.hpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//
// Neutral coroutine task type. Deliberately has no drogon, trantor, or Qt dependency: the Monitor
// and Worker processes (see docs/superpowers/specs/2026-07-26-worker-framework-design.md) link this
// header and have no event loop of drogon's to speak of.
//
// Semantics match drogon::Task and idhan::IDHANTask so it can be swapped in without touching call
// sites: lazy (initial_suspend is suspend_always, so the body does not run until awaited), move-only,
// and symmetric-transferring at both ends of a co_await.
#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

#include "fgl/defines.hpp"

namespace idhan::coro
{

namespace detail
{

//! final_suspend awaiter. Symmetric-transfers straight into the awaiting coroutine rather than
//! returning to the resumer, so an await chain costs one resume regardless of depth. The promise
//! seeds m_continuation with noop_coroutine(), so a task that is started but never awaited by
//! another coroutine simply stops here instead of transferring into a null handle.
struct FinalAwaiter
{
	[[nodiscard]] bool await_ready() const noexcept { return false; }

	template < typename Promise >
	[[nodiscard]] std::coroutine_handle<> await_suspend( std::coroutine_handle< Promise > handle ) const noexcept
	{
		return handle.promise().m_continuation;
	}

	void await_resume() const noexcept {}
};

} // namespace detail

//! Lazy, move-only coroutine return type with no drogon/trantor dependency.
//! \warning Like IDHANTask, this is lazy. Never build one from a capturing lambda and store it to
//!          await later: the closure is destroyed before the body runs, leaving the captures
//!          dangling. Use a captureless lambda or a free function and pass state as parameters,
//!          which are copied into the coroutine frame.
//! \tparam T The co_returned value type. A void specialisation follows.
template < typename T = void >
class [[nodiscard]] Task
{
  public:

	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	struct promise_type
	{
		std::optional< T > m_value {};
		std::exception_ptr m_exception {};
		std::coroutine_handle<> m_continuation { std::noop_coroutine() };

		Task get_return_object() noexcept { return Task { handle_type::from_promise( *this ) }; }

		static std::suspend_always initial_suspend() noexcept { return {}; }

		static detail::FinalAwaiter final_suspend() noexcept { return {}; }

		void return_value( T value ) { m_value.emplace( std::move( value ) ); }

		void unhandled_exception() noexcept { m_exception = std::current_exception(); }

		T&& result() &&
		{
			if ( m_exception ) std::rethrow_exception( m_exception );
			assert( m_value.has_value() && "Task completed without a value and without an exception" );
			return std::move( *m_value );
		}
	};

	struct Awaiter
	{
		handle_type m_coro;

		[[nodiscard]] bool await_ready() const noexcept { return false; }

		//! Records the awaiting coroutine and symmetric-transfers into the task body, starting it.
		[[nodiscard]] std::coroutine_handle<> await_suspend( std::coroutine_handle<> awaiting ) const noexcept
		{
			m_coro.promise().m_continuation = awaiting;
			return m_coro;
		}

		T await_resume() const { return std::move( m_coro.promise() ).result(); }
	};

	explicit Task( const handle_type handle ) noexcept : m_coro( handle ) {}

	Task() = delete;
	FGL_DELETE_COPY( Task );

	Task( Task&& other ) noexcept : m_coro( std::exchange( other.m_coro, {} ) ) {}

	Task& operator=( Task&& other ) noexcept
	{
		if ( this != &other )
		{
			if ( m_coro ) m_coro.destroy();
			m_coro = std::exchange( other.m_coro, {} );
		}
		return *this;
	}

	~Task()
	{
		if ( m_coro ) m_coro.destroy();
	}

	Awaiter operator co_await() const noexcept
	{
		assert( m_coro && "co_await on a moved-from Task" );
		return Awaiter { m_coro };
	}

  private:

	handle_type m_coro {};
};

//! void specialisation, for coroutines that co_return nothing.
template <>
class [[nodiscard]] Task< void >
{
  public:

	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	struct promise_type
	{
		std::exception_ptr m_exception {};
		std::coroutine_handle<> m_continuation { std::noop_coroutine() };

		Task get_return_object() noexcept { return Task { handle_type::from_promise( *this ) }; }

		static std::suspend_always initial_suspend() noexcept { return {}; }

		static detail::FinalAwaiter final_suspend() noexcept { return {}; }

		void return_void() const noexcept {}

		void unhandled_exception() noexcept { m_exception = std::current_exception(); }

		void result() &&
		{
			if ( m_exception ) std::rethrow_exception( m_exception );
		}
	};

	struct Awaiter
	{
		handle_type m_coro;

		[[nodiscard]] bool await_ready() const noexcept { return false; }

		[[nodiscard]] std::coroutine_handle<> await_suspend( std::coroutine_handle<> awaiting ) const noexcept
		{
			m_coro.promise().m_continuation = awaiting;
			return m_coro;
		}

		void await_resume() const { std::move( m_coro.promise() ).result(); }
	};

	explicit Task( const handle_type handle ) noexcept : m_coro( handle ) {}

	Task() = delete;
	FGL_DELETE_COPY( Task );

	Task( Task&& other ) noexcept : m_coro( std::exchange( other.m_coro, {} ) ) {}

	Task& operator=( Task&& other ) noexcept
	{
		if ( this != &other )
		{
			if ( m_coro ) m_coro.destroy();
			m_coro = std::exchange( other.m_coro, {} );
		}
		return *this;
	}

	~Task()
	{
		if ( m_coro ) m_coro.destroy();
	}

	Awaiter operator co_await() const noexcept
	{
		assert( m_coro && "co_await on a moved-from Task" );
		return Awaiter { m_coro };
	}

  private:

	handle_type m_coro {};
};

} // namespace idhan::coro
```

- [ ] **Step 4: Run the tests to verify they pass**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANTests && ./build/bin/IDHANTests --gtest_filter="CoroTask.*" --use_stdout
```

Expected: 6 tests from `CoroTask` run, all PASS.

- [ ] **Step 5: Commit**

```bash
git add IDHAN/include/coro/Task.hpp tests/src/coro/task.cpp
git commit -m "feat(coro): add neutral Task<T> coroutine type with no drogon dependency"
```

---

### Task 2: `Resumer` interface and provider

**Files:**

- Create: `IDHAN/include/coro/Resumer.hpp`
- Create: `IDHAN/src/coro/Resumer.cpp`
- Test: `tests/src/coro/resumer.cpp`

**Interfaces:**

- Consumes: nothing from earlier tasks.
- Produces:
  - `class idhan::coro::Resumer` with `virtual void resume( std::coroutine_handle<> ) noexcept = 0`.
  - `using idhan::coro::ResumerProvider = Resumer* (*)() noexcept;`
  - `void idhan::coro::setResumerProvider( ResumerProvider ) noexcept;`
  - `Resumer* idhan::coro::currentResumer() noexcept;`

  Task 3 implements `RunLoopResumer` against `Resumer`. Tasks 4 and 5 make the awaiters call `currentResumer()` at
  suspend and `Resumer::resume()` at completion. Task 4 also adds the server's provider.

**Why a provider function rather than a single global `Resumer*`:** the existing awaiters call
`trantor::EventLoop::getEventLoopOfCurrentThread()` inside `await_suspend`, so the resumption target depends on **which
thread suspended**. A provider preserves that exactly — the server's provider returns a `thread_local` resumer bound to
the calling thread's loop — while a process with one run loop (Monitor, Worker) just returns the same pointer every
time. There is no allocation on the suspend path either way.

- [ ] **Step 1: Write the failing test**

Create `tests/src/coro/resumer.cpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//

#include <gtest/gtest.h>

#include <coroutine>
#include <vector>

#include "coro/Resumer.hpp"

namespace
{

class RecordingResumer final : public idhan::coro::Resumer
{
  public:

	std::vector< std::coroutine_handle<> > m_seen {};

	void resume( const std::coroutine_handle<> handle ) noexcept override { m_seen.push_back( handle ); }
};

RecordingResumer* g_resumer { nullptr };

idhan::coro::Resumer* provider() noexcept
{
	return g_resumer;
}

} // namespace

TEST( CoroResumer, noProviderInstalledYieldsNull )
{
	idhan::coro::setResumerProvider( nullptr );
	EXPECT_EQ( idhan::coro::currentResumer(), nullptr );
}

TEST( CoroResumer, installedProviderIsReturned )
{
	RecordingResumer resumer {};
	g_resumer = &resumer;
	idhan::coro::setResumerProvider( &provider );

	EXPECT_EQ( idhan::coro::currentResumer(), &resumer );

	idhan::coro::setResumerProvider( nullptr );
	g_resumer = nullptr;
}

TEST( CoroResumer, resumeIsForwardedToTheResumer )
{
	RecordingResumer resumer {};
	g_resumer = &resumer;
	idhan::coro::setResumerProvider( &provider );

	// Typed as coroutine_handle<> rather than noop_coroutine_handle so the EXPECT_EQ below compares
	// like with like.
	const std::coroutine_handle<> handle { std::noop_coroutine() };
	idhan::coro::currentResumer()->resume( handle );

	ASSERT_EQ( resumer.m_seen.size(), 1u );
	EXPECT_EQ( resumer.m_seen.front(), handle );

	idhan::coro::setResumerProvider( nullptr );
	g_resumer = nullptr;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANTests
```

Expected: **compile failure**, `fatal error: coro/Resumer.hpp: No such file or directory`.

- [ ] **Step 3: Create the header**

Create `IDHAN/include/coro/Resumer.hpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//
// Decides where a completed asynchronous operation resumes the coroutine that was awaiting it.
//
// The io backend runs completions on its own watcher thread, but a coroutine generally must not
// resume there: in the server it belongs to a trantor event loop, and in the Monitor and Worker it
// belongs to that process's RunLoop. Awaiters therefore capture a Resumer on the thread that
// suspends and hand the continuation to it from the watcher thread.
#pragma once

#include <coroutine>

#include "fgl/defines.hpp"

namespace idhan::coro
{

//! Hands a coroutine handle back to whatever loop owns it. Captured on the suspending thread,
//! invoked on the io completion thread, so implementations must be safe to call from another thread.
class Resumer
{
  public:

	Resumer() = default;
	virtual ~Resumer() = default;

	FGL_DELETE_COPY( Resumer );
	FGL_DELETE_MOVE( Resumer );

	//! Called on the io completion thread. Must not run the coroutine body inline unless the
	//! implementation genuinely has nowhere else to put it.
	virtual void resume( std::coroutine_handle<> handle ) noexcept = 0;
};

//! Returns the Resumer that a coroutine suspending on the CALLING thread must be resumed through.
//! Implementations are free to return a thread_local (the server does, one per event loop) or a
//! single process-wide object (the Monitor and Worker do).
using ResumerProvider = Resumer* ( * )() noexcept;

//! Installs the process-wide provider. Call once at startup, before any io is submitted. Passing
//! nullptr uninstalls it, which is only useful in tests.
void setResumerProvider( ResumerProvider provider ) noexcept;

//! Called by awaiters on the suspending thread. Returns nullptr when no provider is installed, or
//! when the installed provider has nothing for this thread.
[[nodiscard]] Resumer* currentResumer() noexcept;

} // namespace idhan::coro
```

- [ ] **Step 4: Create the implementation**

Create `IDHAN/src/coro/Resumer.cpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//

#include "coro/Resumer.hpp"

namespace idhan::coro
{

namespace
{
// IDHAN is an OBJECT library, so this global exists once per consumer binary. That is harmless
// because it is only ever read on the io suspend path, and io is only ever submitted by a process
// that called IOUring::init() -- the server today, the Monitor and Worker later. The premade
// modules link IDHAN but never init io, so their copy stays null and unused.
ResumerProvider g_provider { nullptr };
} // namespace

void setResumerProvider( const ResumerProvider provider ) noexcept
{
	g_provider = provider;
}

Resumer* currentResumer() noexcept
{
	return g_provider ? g_provider() : nullptr;
}

} // namespace idhan::coro
```

- [ ] **Step 5: Run the tests to verify they pass**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANTests && ./build/bin/IDHANTests --gtest_filter="CoroResumer.*" --use_stdout
```

Expected: 3 tests from `CoroResumer` run, all PASS.

- [ ] **Step 6: Commit**

```bash
git add IDHAN/include/coro/Resumer.hpp IDHAN/src/coro/Resumer.cpp tests/src/coro/resumer.cpp
git commit -m "feat(coro): add Resumer interface and process-wide provider"
```

---

### Task 3: `RunLoop` and `RunLoopResumer`

**Files:**

- Create: `IDHAN/include/coro/RunLoop.hpp`
- Create: `IDHAN/src/coro/RunLoop.cpp`
- Test: `tests/src/coro/runloop.cpp`

**Interfaces:**

- Consumes: `idhan::coro::Task<T>` (Task 1), `idhan::coro::Resumer` (Task 2).
- Produces:
  - `class idhan::coro::RunLoop` with `void post( std::coroutine_handle<> )`, `void run()`, `void stop()`.
  - `class idhan::coro::RunLoopResumer final : public Resumer`, constructed from a `RunLoop&`.
  - `template < typename T > T idhan::coro::runOnLoop( RunLoop&, Task< T > )` and its `Task<void>` overload.

  Task 9 uses `RunLoop`, `RunLoopResumer` and `runOnLoop` to drive a real io_uring read with no trantor present.
  Plan 3 uses `RunLoop` as the Monitor's and Worker's main loop.

**Why `run()` drains before returning:** `runOnLoop` starts its driver coroutine eagerly, so a task that completes
without ever suspending calls `stop()` **before** `run()` is entered. `run()` must therefore treat "stopping" as "return
once the queue is empty", not "return immediately", or a synchronously-completing task would deadlock on the first
variant and drop queued work on the second. The io layer's synchronous `pread`/`pwrite` fallback (used when
`io_uring_setup` fails, e.g. under a restrictive Docker seccomp profile) hits exactly this path.

- [ ] **Step 1: Write the failing test**

Create `tests/src/coro/runloop.cpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "coro/RunLoop.hpp"
#include "coro/Task.hpp"

namespace
{

idhan::coro::RunLoop* g_loop { nullptr };

//! Suspends, hands its continuation to another thread, and is resumed through the RunLoop -- the
//! same shape the io awaiters have, without needing io_uring.
struct OffThreadAwaiter
{
	std::thread m_worker {};

	[[nodiscard]] bool await_ready() const noexcept { return false; }

	void await_suspend( const std::coroutine_handle<> handle )
	{
		m_worker = std::thread(
			[ handle ]()
			{
				std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
				g_loop->post( handle );
			} );
	}

	void await_resume()
	{
		if ( m_worker.joinable() ) m_worker.join();
	}
};

idhan::coro::Task< int > resumedElsewhere()
{
	co_await OffThreadAwaiter {};
	co_return 7;
}

idhan::coro::Task< int > immediate()
{
	co_return 11;
}

idhan::coro::Task< int > failing()
{
	throw std::runtime_error( "run loop task failed" );
	co_return 0;
}

idhan::coro::Task<> voidTask( std::atomic< int >* counter )
{
	counter->fetch_add( 1 );
	co_return;
}

} // namespace

TEST( CoroRunLoop, resumesATaskCompletedOnAnotherThread )
{
	idhan::coro::RunLoop loop {};
	g_loop = &loop;

	EXPECT_EQ( idhan::coro::runOnLoop( loop, resumedElsewhere() ), 7 );

	g_loop = nullptr;
}

TEST( CoroRunLoop, handlesATaskThatNeverSuspends )
{
	// stop() lands before run() is entered. run() must drain and return rather than block forever.
	idhan::coro::RunLoop loop {};
	EXPECT_EQ( idhan::coro::runOnLoop( loop, immediate() ), 11 );
}

TEST( CoroRunLoop, rethrowsTaskException )
{
	idhan::coro::RunLoop loop {};
	EXPECT_THROW( ( void ) idhan::coro::runOnLoop( loop, failing() ), std::runtime_error );
}

TEST( CoroRunLoop, runsVoidTasks )
{
	idhan::coro::RunLoop loop {};
	std::atomic< int > counter { 0 };

	idhan::coro::runOnLoop( loop, voidTask( &counter ) );

	EXPECT_EQ( counter.load(), 1 );
}

TEST( CoroRunLoop, resumerPostsToTheLoop )
{
	idhan::coro::RunLoop loop {};
	idhan::coro::RunLoopResumer resumer { loop };

	// noop_coroutine is safe to resume and does nothing, so this only proves the handle made it
	// through post() and back out of run().
	resumer.resume( std::noop_coroutine() );
	loop.stop();
	loop.run();

	SUCCEED();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANTests
```

Expected: **compile failure**, `fatal error: coro/RunLoop.hpp: No such file or directory`.

- [ ] **Step 3: Create the header**

Create `IDHAN/include/coro/RunLoop.hpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//
// Minimal coroutine run loop for processes with no trantor event loop -- the Monitor and the Worker
// (docs/superpowers/specs/2026-07-26-worker-framework-design.md S3.2, S3.3), and tests that want to
// exercise the io layer without standing up drogon.
#pragma once

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

#include "coro/Resumer.hpp"
#include "coro/Task.hpp"
#include "fgl/defines.hpp"

namespace idhan::coro
{

//! A queue of coroutine handles pumped by one thread. post() is callable from any thread; run()
//! owns the thread it is called on until stop() is called and the queue has drained.
class RunLoop
{
	std::mutex m_mtx {};
	std::condition_variable m_cv {};
	std::deque< std::coroutine_handle<> > m_queue {};
	bool m_stopping { false };

  public:

	RunLoop() = default;
	~RunLoop() = default;

	FGL_DELETE_COPY( RunLoop );
	FGL_DELETE_MOVE( RunLoop );

	//! Thread-safe. Queues `handle` to be resumed on the thread inside run().
	void post( std::coroutine_handle<> handle );

	//! Pumps queued handles on the calling thread. Returns once stop() has been called AND the queue
	//! is empty, so work queued before or during the stop is never dropped.
	void run();

	//! Thread-safe. Asks run() to return once the queue has drained. Calling this before run() is
	//! entered is legal and makes run() a drain-and-return.
	void stop();
};

//! Resumer that hands continuations to a RunLoop. Safe to call from the io watcher thread.
class RunLoopResumer final : public Resumer
{
	RunLoop* m_loop;

  public:

	explicit RunLoopResumer( RunLoop& loop ) noexcept : m_loop( &loop ) {}

	void resume( std::coroutine_handle<> handle ) noexcept override;
};

namespace detail
{

//! Eagerly-started, self-destroying coroutine used only to drive a Task from non-coroutine code.
struct DetachedTask
{
	struct promise_type
	{
		DetachedTask get_return_object() const noexcept { return {}; }

		static std::suspend_never initial_suspend() noexcept { return {}; }

		static std::suspend_never final_suspend() noexcept { return {}; }

		void return_void() const noexcept {}

		// driveTask catches everything the task can throw, so reaching here means the driver itself
		// failed; there is no caller left to propagate to.
		void unhandled_exception() const { std::terminate(); }
	};
};

template < typename T >
DetachedTask driveTask( Task< T > task, std::optional< T >* out, std::exception_ptr* error, RunLoop* loop )
{
	try
	{
		out->emplace( co_await task );
	}
	catch ( ... )
	{
		*error = std::current_exception();
	}

	loop->stop();
}

inline DetachedTask driveTask( Task< void > task, std::exception_ptr* error, RunLoop* loop )
{
	try
	{
		co_await task;
	}
	catch ( ... )
	{
		*error = std::current_exception();
	}

	loop->stop();
}

} // namespace detail

//! Runs `task` to completion, pumping `loop` on the calling thread until it finishes, and returns
//! its value. Rethrows whatever the task threw.
template < typename T >
T runOnLoop( RunLoop& loop, Task< T > task )
{
	std::optional< T > out {};
	std::exception_ptr error {};

	// The driver starts eagerly, so a task that never suspends is already finished (and has already
	// called stop()) by the time run() is entered. run() handles that by draining and returning.
	detail::driveTask< T >( std::move( task ), &out, &error, &loop );
	loop.run();

	if ( error ) std::rethrow_exception( error );

	return std::move( *out );
}

//! void overload of runOnLoop.
inline void runOnLoop( RunLoop& loop, Task< void > task )
{
	std::exception_ptr error {};

	detail::driveTask( std::move( task ), &error, &loop );
	loop.run();

	if ( error ) std::rethrow_exception( error );
}

} // namespace idhan::coro
```

- [ ] **Step 4: Create the implementation**

Create `IDHAN/src/coro/RunLoop.cpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//

#include "coro/RunLoop.hpp"

namespace idhan::coro
{

void RunLoop::post( const std::coroutine_handle<> handle )
{
	{
		std::lock_guard lock { m_mtx };
		m_queue.push_back( handle );
	}

	m_cv.notify_one();
}

void RunLoop::run()
{
	while ( true )
	{
		std::coroutine_handle<> handle {};

		{
			std::unique_lock lock { m_mtx };
			m_cv.wait( lock, [ this ] { return m_stopping || !m_queue.empty(); } );

			// Woken with nothing left to do means stop() has been called and the queue has drained.
			// The queue is checked before the flag so a stop never discards already-queued work.
			if ( m_queue.empty() ) return;

			handle = m_queue.front();
			m_queue.pop_front();
		}

		// Resumed outside the lock: the coroutine body is free to post() back onto this loop.
		handle.resume();
	}
}

void RunLoop::stop()
{
	{
		std::lock_guard lock { m_mtx };
		m_stopping = true;
	}

	m_cv.notify_all();
}

void RunLoopResumer::resume( const std::coroutine_handle<> handle ) noexcept
{
	m_loop->post( handle );
}

} // namespace idhan::coro
```

- [ ] **Step 5: Run the tests to verify they pass**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANTests && ./build/bin/IDHANTests --gtest_filter="CoroRunLoop.*" --use_stdout
```

Expected: 5 tests from `CoroRunLoop` run, all PASS.

- [ ] **Step 6: Commit**

```bash
git add IDHAN/include/coro/RunLoop.hpp IDHAN/src/coro/RunLoop.cpp tests/src/coro/runloop.cpp
git commit -m "feat(coro): add RunLoop, RunLoopResumer and runOnLoop"
```

---

### Task 4: Point the Linux awaiters at `Resumer`

**Files:**

- Create: `IDHANServer/src/threading/TrantorResumer.hpp`
- Create: `IDHANServer/src/threading/TrantorResumer.cpp`
- Modify: `IDHANServer/src/filesystem/io/linux/ReadAwaiter.hpp:15-18,38`
- Modify: `IDHANServer/src/filesystem/io/linux/ReadAwaiter.cpp:6-12,41,49-52`
- Modify: `IDHANServer/src/filesystem/io/linux/WriteAwaiter.hpp:13-16,36`
- Modify: `IDHANServer/src/filesystem/io/linux/WriteAwaiter.cpp:6-12,24-25,59`
- Modify: `IDHANServer/src/ServerContext.cpp:26,379`

**Interfaces:**

- Consumes: `idhan::coro::Resumer`, `setResumerProvider`, `currentResumer` (Task 2).
- Produces: `idhan::TrantorResumer` and `idhan::coro::Resumer* idhan::trantorResumerForCurrentThread() noexcept`, the
  server's provider. Task 5 does the identical substitution for the Windows awaiters; nothing else consumes these.

**Note on placement:** `TrantorResumer` goes in `IDHANServer/src/threading/`, **not** in `filesystem/io/`. It is the one
piece that must stay behind in the server (it needs trantor), and Task 8 moves the whole of `filesystem/io/` away.
Putting it in `threading/` next to `IDHANTask.hpp` now avoids leaving an orphan directory later.

**Behaviour change to be aware of:** today `complete()` does `m_event_loop->queueInLoop( m_cont )` with no null check,
so an io operation started on a thread with no trantor loop segfaults. The replacement logs and resumes inline instead.
That is strictly better, and no current call site is on such a thread.

- [ ] **Step 1: Create the TrantorResumer header**

Create `IDHANServer/src/threading/TrantorResumer.hpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//
#pragma once

#include <coroutine>

#include "coro/Resumer.hpp"

namespace trantor
{
class EventLoop;
}

namespace idhan
{

//! Resumer bound to one trantor event loop: the loop of the thread it was constructed on.
class TrantorResumer final : public coro::Resumer
{
	trantor::EventLoop* m_loop;

  public:

	TrantorResumer();

	void resume( std::coroutine_handle<> handle ) noexcept override;
};

//! The server's ResumerProvider. Returns a thread_local TrantorResumer bound to the calling thread's
//! event loop, which reproduces the old behaviour of resolving the loop inside await_suspend.
coro::Resumer* trantorResumerForCurrentThread() noexcept;

} // namespace idhan
```

- [ ] **Step 2: Create the TrantorResumer implementation**

Create `IDHANServer/src/threading/TrantorResumer.cpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//

#include "threading/TrantorResumer.hpp"

#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"

namespace idhan
{

TrantorResumer::TrantorResumer() : m_loop( trantor::EventLoop::getEventLoopOfCurrentThread() )
{}

void TrantorResumer::resume( const std::coroutine_handle<> handle ) noexcept
{
	if ( m_loop )
	{
		m_loop->queueInLoop( handle );
		return;
	}

	// The thread that suspended had no trantor loop. The previous code dereferenced the null loop
	// pointer here and crashed; resuming inline on the io watcher thread at least makes progress,
	// at the cost of stalling completion processing for the duration of the coroutine body.
	log::warn( "TrantorResumer: no event loop for the suspending thread, resuming inline on the io thread" );
	handle.resume();
}

coro::Resumer* trantorResumerForCurrentThread() noexcept
{
	// Constructed on first use per thread, which is the thread that is suspending, so it captures
	// that thread's loop.
	thread_local TrantorResumer resumer {};
	return &resumer;
}

} // namespace idhan
```

- [ ] **Step 3: Swap the loop pointer for a Resumer in ReadAwaiter.hpp**

In `IDHANServer/src/filesystem/io/linux/ReadAwaiter.hpp`, replace the trantor forward declaration block:

```cpp
#include "fgl/defines.hpp"

namespace trantor
{
class EventLoop;
}

namespace idhan
{
```

with:

```cpp
#include "coro/Resumer.hpp"
#include "fgl/defines.hpp"

namespace idhan
{
```

Then replace the member:

```cpp
	trantor::EventLoop* m_event_loop { nullptr };
```

with:

```cpp
	coro::Resumer* m_resumer { nullptr };
```

- [ ] **Step 4: Use the Resumer in ReadAwaiter.cpp**

In `IDHANServer/src/filesystem/io/linux/ReadAwaiter.cpp`, replace the include block:

```cpp
#include "filesystem/io/linux/IOUringLinux.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"
```

with:

```cpp
#include "coro/Resumer.hpp"
#include "filesystem/io/linux/IOUringLinux.hpp"
#include "logging/log.hpp"
```

Replace the tail of `complete()`:

```cpp
	m_event_loop->queueInLoop( m_cont );
```

with:

```cpp
	if ( m_resumer )
	{
		m_resumer->resume( m_cont );
		return;
	}

	// No Resumer was installed on the suspending thread. Resuming inline on the watcher thread is
	// the only remaining option, and it stalls completion processing while the body runs.
	log::critical( "ReadAwaiter had no Resumer; resuming inline on the io watcher thread" );
	m_cont.resume();
```

Replace the first line of `await_suspend()`:

```cpp
	m_event_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
```

with:

```cpp
	m_resumer = coro::currentResumer();
```

- [ ] **Step 5: Apply the same change to WriteAwaiter.hpp**

In `IDHANServer/src/filesystem/io/linux/WriteAwaiter.hpp`, replace:

```cpp
#include "fgl/defines.hpp"

namespace trantor
{
class EventLoop;
}

namespace idhan
{
```

with:

```cpp
#include "coro/Resumer.hpp"
#include "fgl/defines.hpp"

namespace idhan
{
```

Then replace the member:

```cpp
	trantor::EventLoop* m_event_loop { nullptr };
```

with:

```cpp
	coro::Resumer* m_resumer { nullptr };
```

- [ ] **Step 6: Apply the same change to WriteAwaiter.cpp**

In `IDHANServer/src/filesystem/io/linux/WriteAwaiter.cpp`, replace the include block:

```cpp
#include "filesystem/io/linux/IOUringLinux.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"
```

with:

```cpp
#include "coro/Resumer.hpp"
#include "filesystem/io/linux/IOUringLinux.hpp"
#include "logging/log.hpp"
```

Replace the first line of `await_suspend()`:

```cpp
	m_event_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
```

with:

```cpp
	m_resumer = coro::currentResumer();
```

Replace the last line of `complete()`:

```cpp
	if ( m_cont ) m_event_loop->queueInLoop( m_cont );
```

with:

```cpp
	if ( !m_cont ) return;

	if ( m_resumer )
	{
		m_resumer->resume( m_cont );
		return;
	}

	log::critical( "WriteAwaiter had no Resumer; resuming inline on the io watcher thread" );
	m_cont.resume();
```

- [ ] **Step 7: Install the provider in ServerContext.cpp**

In `IDHANServer/src/ServerContext.cpp`, add to the include block (keeping it alphabetically ordered alongside the
existing `#include "filesystem/io/IOUring.hpp"` at line 26):

```cpp
#include "coro/Resumer.hpp"
#include "threading/TrantorResumer.hpp"
```

Then replace:

```cpp
	// Must happen before anything can touch FileIOUring (e.g. ClusterManager reading/writing files);
	// IOUring::getInstance() throws if init() was never called.
	IOUring::init();
```

with:

```cpp
	// Must happen before anything can touch FileIOUring (e.g. ClusterManager reading/writing files);
	// IOUring::getInstance() throws if init() was never called. The Resumer provider has to be in
	// place first: the io awaiters capture it when they suspend, and without one they would resume
	// inline on the io watcher thread instead of on the awaiting coroutine's event loop.
	coro::setResumerProvider( &trantorResumerForCurrentThread );
	IOUring::init();
```

- [ ] **Step 8: Verify the build**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: clean build. If the compiler reports `m_event_loop` is still referenced, a substitution above was missed —
`grep -rn "m_event_loop" IDHANServer/src/filesystem/io/linux/` must come back empty.

- [ ] **Step 9: Commit**

```bash
git add IDHANServer/src/threading/TrantorResumer.hpp IDHANServer/src/threading/TrantorResumer.cpp \
        IDHANServer/src/filesystem/io/linux/ReadAwaiter.hpp IDHANServer/src/filesystem/io/linux/ReadAwaiter.cpp \
        IDHANServer/src/filesystem/io/linux/WriteAwaiter.hpp IDHANServer/src/filesystem/io/linux/WriteAwaiter.cpp \
        IDHANServer/src/ServerContext.cpp
git commit -m "refactor(io): resume Linux io awaiters through Resumer instead of trantor directly"
```

---

### Task 5: Point the Windows awaiters at `Resumer`

**Files:**

- Modify: `IDHANServer/src/filesystem/io/windows/WinAwaiters.hpp:17-19,37`
- Modify: `IDHANServer/src/filesystem/io/windows/WinAwaiters.cpp:9,35-38,48,73-76,86`

**Interfaces:**

- Consumes: `idhan::coro::Resumer`, `currentResumer` (Task 2). Same substitution as Task 4.
- Produces: nothing new.

**Why this is a separate task:** it cannot be compiled on the Linux development machine, so it must be reviewable and
revertible on its own. Everything in this task is inside `#ifdef _WIN32`-guarded files, so a mistake here cannot break
the Linux build — but it also means the build verification below only proves the Linux tree still compiles.

**Simpler than Task 4:** these two `complete()` implementations already null-check before dispatching and already fall
back to `m_cont.resume()`, so this is a pure rename of the member plus a swap of how it is acquired. No new branch is
introduced and no logging is added.

- [ ] **Step 1: Swap the loop pointer for a Resumer in WinAwaiters.hpp**

In `IDHANServer/src/filesystem/io/windows/WinAwaiters.hpp`, replace the trantor forward declaration:

```cpp
namespace trantor
{
class EventLoop;
}
```

with an include, placed with the other includes at the top of the file:

```cpp
#include "coro/Resumer.hpp"
```

Then replace the member:

```cpp
	trantor::EventLoop* m_event_loop { nullptr };
```

with:

```cpp
	coro::Resumer* m_resumer { nullptr };
```

- [ ] **Step 2: Use the Resumer in WinAwaiters.cpp**

In `IDHANServer/src/filesystem/io/windows/WinAwaiters.cpp`, replace the include:

```cpp
#include "trantor/net/EventLoop.h"
```

with:

```cpp
#include "coro/Resumer.hpp"
```

- [ ] **Step 3: Swap the dispatch in both complete() implementations**

In `ReadAwaiterWin::complete`, replace:

```cpp
	if ( m_event_loop )
		m_event_loop->queueInLoop( m_cont );
	else
		m_cont.resume();
```

with:

```cpp
	if ( m_resumer )
		m_resumer->resume( m_cont );
	else
		m_cont.resume();
```

In `WriteAwaiterWin::complete`, replace the identical block:

```cpp
	if ( m_event_loop )
		m_event_loop->queueInLoop( m_cont );
	else
		m_cont.resume();
```

with:

```cpp
	if ( m_resumer )
		m_resumer->resume( m_cont );
	else
		m_cont.resume();
```

- [ ] **Step 4: Swap the acquisition in both await_suspend implementations**

In `ReadAwaiterWin::await_suspend` and again in `WriteAwaiterWin::await_suspend`, replace:

```cpp
	m_event_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
```

with:

```cpp
	m_resumer = coro::currentResumer();
```

Then confirm the member is gone from the file:

```bash
grep -n "m_event_loop" IDHANServer/src/filesystem/io/windows/WinAwaiters.cpp
```

Expected: **no output**.

- [ ] **Step 5: Verify no trantor references remain in the io layer**

Ask the user to run:

```bash
grep -rn "trantor" IDHANServer/src/filesystem/io/
```

Expected: **no output**. Then:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: clean build (this only exercises the Linux backend; the Windows files are `#ifdef _WIN32`-guarded).

- [ ] **Step 6: Commit**

```bash
git add IDHANServer/src/filesystem/io/windows/WinAwaiters.hpp IDHANServer/src/filesystem/io/windows/WinAwaiters.cpp
git commit -m "refactor(io): resume Windows io awaiters through Resumer instead of trantor directly"
```

---

### Task 6: Retype the io API from `drogon::Task` to `coro::Task`

**Files:**

- Modify: `IDHANServer/src/filesystem/io/IOUring.hpp:11,25,30,92,96,98`
- Modify: `IDHANServer/src/filesystem/io/IOUring.cpp:15`
- Modify: `IDHANServer/src/filesystem/io/linux/IOUringLinux.hpp:124,126`
- Modify: `IDHANServer/src/filesystem/io/linux/IOUringLinux.cpp:17,90,99,379,408`
- Modify: `IDHANServer/src/filesystem/io/windows/IOUringW10.hpp:46,48`
- Modify: `IDHANServer/src/filesystem/io/windows/IOUringW11.hpp:48,50`
- Modify: `IDHANServer/src/filesystem/io/windows/IOUringW10.cpp:105,140`
- Modify: `IDHANServer/src/filesystem/io/windows/IOUringW11.cpp:107,141`
- Modify: `IDHANServer/src/filesystem/io/windows/IOUringWindows.cpp:92,101`

**Interfaces:**

- Consumes: `idhan::coro::Task<T>` (Task 1).
- Produces: the io layer's public signatures become
  - `coro::Task< std::vector< std::byte > > IOUring::read( NativeHandle, std::size_t, std::size_t )`
  - `coro::Task< void > IOUring::write( NativeHandle, std::vector< std::byte >, std::size_t )`
  - `coro::Task< std::vector< std::byte > > FileIOUring::read( std::size_t, std::size_t ) const`
  - `coro::Task< std::vector< std::byte > > FileIOUring::readAll() const`
  - `coro::Task< void > FileIOUring::write( std::vector< std::byte >, std::size_t ) const`

  Task 8 moves these files; Task 9 calls `readAll()` from a `coro::Task`.

**No consumer file is touched by this task.** `co_await` resolves through `operator co_await()` on whatever the
expression's type is; the awaiting coroutine's own type is irrelevant, and neither `drogon::Task` nor `IDHANTask`
defines an `await_transform` that would intercept it. All nine consumers keep compiling unchanged. If the build
disagrees, stop and report rather than editing consumers — that would mean one of them stores an io task instead of
awaiting it directly, which contradicts the planning-time survey recorded in the Global Constraints.

- [ ] **Step 1: Retype the public header**

In `IDHANServer/src/filesystem/io/IOUring.hpp`, replace the include:

```cpp
#include "drogon/utils/coroutine.h"
```

with:

```cpp
#include "coro/Task.hpp"
```

Then replace every `drogon::Task<` in the file with `coro::Task<`. There are five, in this order: `IOUring::read`,
`IOUring::write`, `FileIOUring::read`, `FileIOUring::readAll`, `FileIOUring::write`. After the edit the declarations
read:

```cpp
	virtual coro::Task< std::vector< std::byte > > read(
		NativeHandle handle,
		std::size_t offset,
		std::size_t len ) = 0;

	virtual coro::Task< void > write( NativeHandle handle, std::vector< std::byte > data, std::size_t offset ) = 0;
```

and:

```cpp
	[[nodiscard]] coro::Task< std::vector< std::byte > > read( std::size_t offset, std::size_t len ) const;

	//! Reads the whole file into one owned buffer. Chunked at 512 MiB because io_uring's per-SQE
	//! length field is a __u32, so no single op can cover a file of 4 GiB or more.
	[[nodiscard]] coro::Task< std::vector< std::byte > > readAll() const;

	[[nodiscard]] coro::Task< void > write( std::vector< std::byte > data, std::size_t offset = 0 ) const;
```

- [ ] **Step 2: Retype IOUring.cpp**

In `IDHANServer/src/filesystem/io/IOUring.cpp`, replace:

```cpp
drogon::Task< std::vector< std::byte > > FileIOUring::readAll() const
```

with:

```cpp
coro::Task< std::vector< std::byte > > FileIOUring::readAll() const
```

- [ ] **Step 3: Retype the Linux backend**

In `IDHANServer/src/filesystem/io/linux/IOUringLinux.hpp`, replace:

```cpp
	drogon::Task< std::vector< std::byte > > read( NativeHandle handle, std::size_t offset, std::size_t len ) override;

	drogon::Task< void > write( NativeHandle handle, std::vector< std::byte > data, std::size_t offset ) override;
```

with:

```cpp
	coro::Task< std::vector< std::byte > > read( NativeHandle handle, std::size_t offset, std::size_t len ) override;

	coro::Task< void > write( NativeHandle handle, std::vector< std::byte > data, std::size_t offset ) override;
```

In `IDHANServer/src/filesystem/io/linux/IOUringLinux.cpp`, remove the now-unneeded drogon include:

```cpp
#include "drogon/HttpAppFramework.h"
```

Then replace the four definition signatures. `FileIOUring::read`:

```cpp
coro::Task< std::vector< std::byte > > FileIOUring::read( const std::size_t offset, std::size_t len ) const
```

`FileIOUring::write`:

```cpp
coro::Task< void > FileIOUring::write( const std::vector< std::byte > data, const std::size_t offset ) const
```

`IOUringLinux::read`:

```cpp
coro::Task< std::vector< std::byte > > IOUringLinux::read(
	const NativeHandle handle,
	const std::size_t offset,
	const std::size_t len )
```

`IOUringLinux::write`:

```cpp
coro::Task< void > IOUringLinux::write(
	const NativeHandle handle,
	std::vector< std::byte > data,
	const std::size_t offset )
```

- [ ] **Step 4: Retype the Windows backends**

Apply the same mechanical substitution — `drogon::Task<` becomes `coro::Task<` — to every declaration and definition
in these four files:

- `IDHANServer/src/filesystem/io/windows/IOUringW10.hpp` (2 declarations: `read`, `write`)
- `IDHANServer/src/filesystem/io/windows/IOUringW11.hpp` (2 declarations: `read`, `write`)
- `IDHANServer/src/filesystem/io/windows/IOUringW10.cpp` (2 definitions: `IOUringW10::read`, `IOUringW10::write`)
- `IDHANServer/src/filesystem/io/windows/IOUringW11.cpp` (2 definitions: `IOUringW11::read`, `IOUringW11::write`)
- `IDHANServer/src/filesystem/io/windows/IOUringWindows.cpp` (2 definitions: `FileIOUring::read`, `FileIOUring::write`)

Then confirm nothing was missed:

```bash
grep -rn "drogon" IDHANServer/src/filesystem/io/
```

Expected: **no output**.

- [ ] **Step 5: Verify the build**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: clean build, with no edits to any consumer file.

- [ ] **Step 6: Commit**

```bash
git add IDHANServer/src/filesystem/io/IOUring.hpp IDHANServer/src/filesystem/io/IOUring.cpp \
        IDHANServer/src/filesystem/io/linux/IOUringLinux.hpp IDHANServer/src/filesystem/io/linux/IOUringLinux.cpp \
        IDHANServer/src/filesystem/io/windows/IOUringW10.hpp IDHANServer/src/filesystem/io/windows/IOUringW10.cpp \
        IDHANServer/src/filesystem/io/windows/IOUringW11.hpp IDHANServer/src/filesystem/io/windows/IOUringW11.cpp \
        IDHANServer/src/filesystem/io/windows/IOUringWindows.cpp
git commit -m "refactor(io): return coro::Task from the io backend instead of drogon::Task"
```

---

### Task 7: Move logging into `IDHAN`

**Files:**

- Move: `IDHANServer/src/logging/log.hpp` to `IDHAN/include/logging/log.hpp`
- Move: `IDHANServer/src/logging/log.cpp` to `IDHAN/src/logging/log.cpp`
- Delete: `IDHANServer/src/logging/qt_formatters/qstring.hpp`
- Modify: `IDHAN/CMakeLists.txt`

**Interfaces:**

- Consumes: nothing from earlier tasks.
- Produces: `idhan::log::{trace,debug,info,warn,error,critical}` and the `getServerLogger` /
  `getServerRingBufferSink` / `setServerLogger` accessors become available to anything linking `IDHAN`. Task 8 needs
  this, because the io sources it moves all `#include "logging/log.hpp"`.

**Why this must happen before the io move:** every io translation unit logs. If the io sources moved into `IDHAN` while
`log.hpp` was still under `IDHANServer/src/`, `IDHAN` would not compile.

**Why no consumer edits are needed:** server sources already write `#include "logging/log.hpp"`. Today that resolves
through `IDHANServer/src` (added PRIVATE by `AddFGLExecutable`); afterwards it resolves through `IDHAN/include` (added
PUBLIC by `AddFGLLibrary` and inherited via the `IDHANServer -> IDHAN` link). The spelling is identical either way.

**Why the Qt formatter include is dropped:** `log.hpp` currently pulls in `qt_formatters/qstring.hpp`, which needs
`<QString>`. Keeping it would force `Qt6::Core` onto `IDHAN`, and from there onto the Monitor and Worker in Plan 3.
Verified at planning time: the only `QString` use anywhere in `IDHANServer/src` is two `QString::fromStdString` calls
in `filesystem/clusters/ClusterManager.cpp`, neither of which is a log argument, so nothing in the server formats a
`QString`. `IDHANClient` is unaffected — it includes `logging/qt_formatters/qstring.hpp` directly from
`IDHANClient/include/idhan/logging/logger.hpp`, and `IDHAN/include/logging/qt_formatters/qstring.hpp` stays exactly
where it is.

- [ ] **Step 1: Move the files**

```bash
mkdir -p IDHAN/src/logging
git mv IDHANServer/src/logging/log.hpp IDHAN/include/logging/log.hpp
git mv IDHANServer/src/logging/log.cpp IDHAN/src/logging/log.cpp
git rm IDHANServer/src/logging/qt_formatters/qstring.hpp
```

- [ ] **Step 2: Drop the Qt formatter include from the moved header**

In `IDHAN/include/logging/log.hpp`, replace:

```cpp
#include "logging/format_ns.hpp"
#include "qt_formatters/qstring.hpp"
```

with:

```cpp
#include "logging/format_ns.hpp"
```

- [ ] **Step 3: Fix the include in the moved source**

In `IDHAN/src/logging/log.cpp`, replace:

```cpp
#include "log.hpp"
```

with:

```cpp
#include "logging/log.hpp"
```

- [ ] **Step 4: Link spdlog to IDHAN**

In `IDHAN/CMakeLists.txt`, replace the final line:

```cmake
target_link_libraries(IDHAN PUBLIC libFGL Jsoncpp_lib)
```

with:

```cmake
# spdlog is PUBLIC: logging/log.hpp is a header consumers include, and its templates call into
# spdlog directly.
find_package(spdlog REQUIRED)

target_link_libraries(IDHAN PUBLIC libFGL Jsoncpp_lib spdlog::spdlog)
```

- [ ] **Step 5: Verify the build**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: clean build. A `QString` formatter error would mean the planning-time survey was wrong; if that happens, add
`#include "logging/qt_formatters/qstring.hpp"` to the specific offending translation unit rather than putting it back
into `log.hpp`, and report it.

- [ ] **Step 6: Commit**

```bash
git add IDHAN/include/logging/log.hpp IDHAN/src/logging/log.cpp IDHAN/CMakeLists.txt \
        IDHANServer/src/logging/log.hpp IDHANServer/src/logging/log.cpp \
        IDHANServer/src/logging/qt_formatters/qstring.hpp
git commit -m "refactor(logging): move log.hpp and log.cpp into the IDHAN library"
```

---

### Task 8: Move the io layer into `IDHAN`

**Files:**

- Move: `IDHANServer/src/filesystem/io/IOUring.hpp` to `IDHAN/include/io/IOUring.hpp`
- Move: `IDHANServer/src/filesystem/io/IOUring.cpp` to `IDHAN/src/io/IOUring.cpp`
- Move: `IDHANServer/src/filesystem/io/linux/` to `IDHAN/src/io/linux/`
- Move: `IDHANServer/src/filesystem/io/windows/` to `IDHAN/src/io/windows/`
- Modify: `IDHAN/CMakeLists.txt`
- Modify: `IDHANServer/CMakeLists.txt:13-15`
- Modify (include line only): `IDHANServer/src/ServerContext.cpp:26`,
  `IDHANServer/src/api/cluster/scan.cpp:16`, `IDHANServer/src/api/record/fetchThumbnail.cpp:16`,
  `IDHANServer/src/crypto/SHA256.cpp:18`, `IDHANServer/src/metadata/updateRecordMetadata.cpp:10`,
  `IDHANServer/src/mime/MimeDatabase.hpp:13`, `IDHANServer/src/mime/Cursor.hpp:13`,
  `IDHANServer/src/mime/Cursor.cpp:9`, `IDHANServer/src/mime/FileInfo.cpp:7`

**Interfaces:**

- Consumes: `logging/log.hpp` from `IDHAN` (Task 7), `coro/Task.hpp` (Task 1), `coro/Resumer.hpp` (Task 2).
- Produces: `idhan::IOUring` and `idhan::FileIOUring` reachable from any target linking `IDHAN`, via
  `#include "io/IOUring.hpp"`. Task 9 and Plans 2 and 3 depend on this.

**Header placement:** only `IOUring.hpp` is public API, so it is the only header that goes to `IDHAN/include/`. The
backend headers (`IOUringLinux.hpp`, `ReadAwaiter.hpp`, `WriteAwaiter.hpp`, and the Windows set) stay next to their
sources under `IDHAN/src/io/`, which `AddFGLLibrary` adds as a PRIVATE include directory — so they remain internal to
`IDHAN` exactly as they are internal to `IDHANServer` today.

- [ ] **Step 1: Move the files**

```bash
mkdir -p IDHAN/include/io IDHAN/src/io
git mv IDHANServer/src/filesystem/io/IOUring.hpp IDHAN/include/io/IOUring.hpp
git mv IDHANServer/src/filesystem/io/IOUring.cpp IDHAN/src/io/IOUring.cpp
git mv IDHANServer/src/filesystem/io/linux IDHAN/src/io/linux
git mv IDHANServer/src/filesystem/io/windows IDHAN/src/io/windows
```

Then confirm the source directory is gone:

```bash
ls IDHANServer/src/filesystem/
```

Expected: `clusters` only. If `io` still exists and is non-empty, something was left behind — report it.

- [ ] **Step 2: Repoint the includes inside the moved files**

Every `filesystem/io/` include inside the moved tree loses its `filesystem/` prefix:

```bash
grep -rl "filesystem/io/" IDHAN/src/io/ IDHAN/include/io/ \
  | xargs -r sed -i 's|"filesystem/io/|"io/|g'
```

`-r` matters: without it, a grep that matches nothing leaves `sed` reading from stdin and the command hangs.

Then verify:

```bash
grep -rn "filesystem/io" IDHAN/
```

Expected: **no output**.

- [ ] **Step 3: Repoint the nine consumer includes**

```bash
sed -i 's|#include "filesystem/io/IOUring.hpp"|#include "io/IOUring.hpp"|' \
  IDHANServer/src/ServerContext.cpp \
  IDHANServer/src/api/cluster/scan.cpp \
  IDHANServer/src/api/record/fetchThumbnail.cpp \
  IDHANServer/src/crypto/SHA256.cpp \
  IDHANServer/src/metadata/updateRecordMetadata.cpp \
  IDHANServer/src/mime/MimeDatabase.hpp \
  IDHANServer/src/mime/Cursor.hpp \
  IDHANServer/src/mime/Cursor.cpp \
  IDHANServer/src/mime/FileInfo.cpp
```

Then verify nothing anywhere still refers to the old path:

```bash
grep -rn "filesystem/io" IDHANServer/ IDHAN/ tests/
```

Expected: **no output**.

- [ ] **Step 4: Link liburing to IDHAN**

In `IDHAN/CMakeLists.txt`, below the `target_link_libraries(IDHAN PUBLIC ...)` line added in Task 7, add:

```cmake
# The io_uring backend now lives in this library (io/linux). PUBLIC so the server, the tests, and
# the Monitor and Worker binaries added in plan 3 all pick it up through their IDHAN link.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(IDHAN PUBLIC uring)
endif ()
```

- [ ] **Step 5: Drop the now-redundant liburing link from the server**

In `IDHANServer/CMakeLists.txt`, delete these three lines:

```cmake
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(IDHANServer PRIVATE uring)
endif ()
```

`IDHANServer` links `IDHAN`, and `IDHAN` now carries `uring` as a PUBLIC dependency, so the server still gets it.

- [ ] **Step 6: Verify the build**

Ask the user to run:

```bash
cmake --build build/debug --target IDHANServer
```

Expected: clean build. CMake reconfigures itself here because `AddFGLLibrary`/`AddFGLExecutable` glob sources with
`CONFIGURE_DEPENDS`, so the moved files are picked up without a manual reconfigure. If the link fails with undefined
`io_uring_*` symbols, the `uring` link in Step 4 did not take effect — reconfigure explicitly with
`cmake -B build/debug` and try again.

- [ ] **Step 7: Commit**

```bash
git add IDHAN/include/io IDHAN/src/io IDHAN/CMakeLists.txt IDHANServer/CMakeLists.txt \
        IDHANServer/src/filesystem/io \
        IDHANServer/src/ServerContext.cpp IDHANServer/src/api/cluster/scan.cpp \
        IDHANServer/src/api/record/fetchThumbnail.cpp IDHANServer/src/crypto/SHA256.cpp \
        IDHANServer/src/metadata/updateRecordMetadata.cpp IDHANServer/src/mime/MimeDatabase.hpp \
        IDHANServer/src/mime/Cursor.hpp IDHANServer/src/mime/Cursor.cpp IDHANServer/src/mime/FileInfo.cpp
git commit -m "refactor(io): move the io_uring backend from IDHANServer into the IDHAN library"
```

---

### Task 9: Prove the io layer runs with no trantor present

**Files:**

- Test: `tests/src/io/ioUringRunLoop.cpp`

**Interfaces:**

- Consumes: `coro::Task` (Task 1), `coro::setResumerProvider` (Task 2), `coro::RunLoop` / `RunLoopResumer` /
  `runOnLoop` (Task 3), `idhan::FileIOUring` and `idhan::IOUring::init()` at their new `IDHAN` home (Task 8).
- Produces: nothing consumed later. This is the acceptance test for the whole plan.

**What this proves:** that a process with no drogon and no trantor event loop can open a file, submit a real io_uring
read, and have the completion resume its coroutine — which is precisely what `IDHANWorker` and `IDHANMonitor` will do
in Plan 3. Before this plan, `IDHANTests` could not link `FileIOUring` at all: it lived in the server **executable**.

**Note on `IOUring::init()`:** it installs a process-wide singleton and starts a watcher thread, and there is no
teardown entry point. Exactly one test in the binary may call it, and it must be this one. Do not add a second test
that calls `init()`.

- [ ] **Step 1: Write the failing test**

Create `tests/src/io/ioUringRunLoop.cpp`:

```cpp
//
// Created by kj16609 on 7/27/26.
//
// Drives a real io_uring read from a process with no drogon and no trantor event loop, which is the
// arrangement IDHANMonitor and IDHANWorker use in plan 3 of the worker framework.

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "coro/Resumer.hpp"
#include "coro/RunLoop.hpp"
#include "coro/Task.hpp"
#include "io/IOUring.hpp"

namespace
{

idhan::coro::RunLoopResumer* g_resumer { nullptr };

idhan::coro::Resumer* provider() noexcept
{
	return g_resumer;
}

// Free function, not a capturing lambda: coro::Task is lazy, so a closure would already be gone by
// the time the body runs. Parameters are copied into the coroutine frame.
idhan::coro::Task< std::string > readFileToString( std::filesystem::path path )
{
	const idhan::FileIOUring file { path };
	const auto bytes { co_await file.readAll() };

	co_return std::string { reinterpret_cast< const char* >( bytes.data() ), bytes.size() };
}

} // namespace

TEST( IOUringRunLoop, readAllWithoutTrantor )
{
	const auto path { std::filesystem::temp_directory_path() / "idhan-iouring-runloop-test.bin" };

	// Larger than one page so the read is not trivially satisfied, but far below the 512 MiB
	// chunking threshold in readAll(), so this exercises its single-op fast path.
	const std::string contents( 40'000, 'k' );
	{
		std::ofstream out { path, std::ios::binary | std::ios::trunc };
		out.write( contents.data(), static_cast< std::streamsize >( contents.size() ) );
	}

	idhan::coro::RunLoop loop {};
	idhan::coro::RunLoopResumer resumer { loop };
	g_resumer = &resumer;
	idhan::coro::setResumerProvider( &provider );

	idhan::IOUring::init();

	const auto result { idhan::coro::runOnLoop( loop, readFileToString( path ) ) };

	EXPECT_EQ( result.size(), contents.size() );
	EXPECT_EQ( result, contents );

	std::filesystem::remove( path );
}
```

- [ ] **Step 2: Run the test to verify it fails**

If Task 8 has not been applied yet, this fails to compile on `io/IOUring.hpp`. If it has, it should already pass —
which is the expected outcome here, because Tasks 1 through 8 are what make it work. Ask the user to run:

```bash
cmake --build build/debug --target IDHANTests && ./build/bin/IDHANTests --gtest_filter="IOUringRunLoop.*" --use_stdout
```

Expected: 1 test runs and PASSES.

If it hangs, the completion is not reaching the loop: check that `setResumerProvider` is called before
`IOUring::init()` and that `RunLoop::run()` drains rather than returning on the stop flag alone.

If it fails with a read error rather than hanging, io_uring setup may have failed on this machine (the log line reads
`io_uring_setup failed ... using synchronous I/O`). That path is still a valid pass: the synchronous `pread` fallback
completes without suspending, and the test asserts the same bytes. Report which path ran.

- [ ] **Step 3: Commit**

```bash
git add tests/src/io/ioUringRunLoop.cpp
git commit -m "test(io): drive an io_uring read from a RunLoop with no trantor present"
```

---

### Task 10: Full verification and spec update

**Files:**

- Modify: `docs/superpowers/specs/2026-07-26-worker-framework-design.md:246-255`

**Interfaces:**

- Consumes: everything above.
- Produces: a spec whose section 10 records the decisions this plan actually made, so Plans 2 and 3 are written against
  settled ground.

- [ ] **Step 1: Confirm the decoupling is complete**

Ask the user to run:

```bash
grep -rn "drogon\|trantor" IDHAN/include/io IDHAN/src/io IDHAN/include/coro IDHAN/src/coro
```

Expected: **no output**. Any hit is a leftover coupling and must be fixed before this task is considered done.

- [ ] **Step 2: Confirm the whole tree still builds**

Ask the user to run:

```bash
cmake --build build/debug -j$(nproc)
```

Expected: every target builds — `IDHANServer`, `IDHANClient`, `IDHANPremadeModules`, `HydrusImporter`, `IDHANTests`.
This is the first step that exercises the non-server consumers of `IDHAN`, which now carry the io objects and the
`uring` link.

- [ ] **Step 3: Run the full set of new tests**

Ask the user to run:

```bash
./build/bin/IDHANTests --gtest_filter="CoroTask.*:CoroResumer.*:CoroRunLoop.*:IOUringRunLoop.*" --use_stdout
```

Expected: 15 tests, all PASS (6 `CoroTask`, 3 `CoroResumer`, 5 `CoroRunLoop`, 1 `IOUringRunLoop`).

- [ ] **Step 4: Ask the user to exercise the three real io paths in a running server**

The unit tests cover the io layer in isolation but not the server's trantor resumption path, which is what Task 4
rewrote. Ask the user to start the server and confirm all three still work, reporting any error or hang:

1. **Thumbnail fetch** — `GET /record/{id}/thumbnail` for a record that already has a thumbnail on disk
   (`api/record/fetchThumbnail.cpp`).
2. **Cluster scan** — trigger a scan over a cluster with files in it (`api/cluster/scan.cpp`).
3. **Metadata parse** — import or re-parse a file so `metadata/parseMetadata.cpp` reads it.

A hang in any of these means a completion is not being handed back to the right event loop; a crash on startup means
the provider is being installed too late.

- [ ] **Step 5: Record the resolved decisions in the spec**

In `docs/superpowers/specs/2026-07-26-worker-framework-design.md`, replace section 10 in full:

```markdown
## 10. Implementation notes (resolved)

Resolved during planning of `docs/superpowers/plans/2026-07-27-worker-framework-1-io-core.md` and implemented there.

- **Extent of the move.** All of `IDHANServer/src/filesystem/io/` moved: `IOUring.hpp` to `IDHAN/include/io/`, and
  `IOUring.cpp` plus the `linux/` and `windows/` backend directories to `IDHAN/src/io/`. Only `IOUring.hpp` is public;
  the backend headers stay beside their sources on `IDHAN`'s PRIVATE include path. `filesystem/clusters/` stays in the
  server. `logging/log.hpp` and `log.cpp` moved to `IDHAN` as well, because every io translation unit logs; the Qt
  formatter include was dropped from `log.hpp` so `IDHAN` does not acquire a `Qt6::Core` dependency.
- **Neutral coroutine type.** `idhan::coro::Task<T>` in `IDHAN/include/coro/Task.hpp`. Lazy, move-only, symmetric
  transfer at both ends, matching `drogon::Task` semantics so call sites needed no change.
- **Monitor and Worker run loop.** Completions are **not** resumed inline on the io watcher thread. `idhan::coro::RunLoop`
  (`IDHAN/include/coro/RunLoop.hpp`) is a mutex-and-condvar queue of coroutine handles pumped by the process's main
  thread; `RunLoopResumer` posts to it. This mirrors trantor's `queueInLoop` semantics, so the server and the
  Monitor/Worker behave identically and no coroutine body ever runs on the watcher thread.
- **Resumption affinity.** `idhan::coro::Resumer` is an interface; a process installs a `ResumerProvider` function once
  at startup via `setResumerProvider`, and awaiters call `currentResumer()` when they suspend. The server installs
  `trantorResumerForCurrentThread` (`IDHANServer/src/threading/TrantorResumer.hpp`), which returns a `thread_local`
  `TrantorResumer` bound to the calling thread's loop -- reproducing the old
  `getEventLoopOfCurrentThread()`-inside-`await_suspend` behaviour exactly.
- **Library placement.** The transport core lives in `IDHAN`, which is an `OBJECT` library, so its objects are copied
  into every consumer including the `dlopen`'d `IDHANPremadeModules`. This is accepted: both file-scope globals
  involved (`g_provider`, `g_linux_instance`) are inert unless `IOUring::init()` is called, and only the server, the
  Monitor and the Worker call it.

Still open, to be settled in plan 2:

- Whether the monitor client lives in `IDHAN` or `IDHANServer` (leaning `IDHAN`, since it is generic channel/RPC
  plumbing).
- Frame header exact field widths and endianness (fix a canonical little-endian on-wire layout for the future remote
  case).
```

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-07-26-worker-framework-design.md
git commit -m "docs: record the io core decisions resolved during plan 1"
```

- [ ] **Step 7: Report**

Summarise for the user: which build commands were run and their outcome, the test counts and results, which of the
three manual io paths were exercised and whether each worked, and whether io_uring or the synchronous fallback was in
play on their machine. Then confirm they are ready for Plan 2 (transport: frame header, `Channel`, `Blob`).
