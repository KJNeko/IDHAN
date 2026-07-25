//
// Dependency-level coroutine-fiber helpers shared by IDHAN's coroutine promises and the
// vendored-drogon Tracy patch. The pure name helpers are always defined (cheap, no Tracy dep)
// so they remain unit-testable; the awaiter machinery is gated on TRACY_ENABLE.
//
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace idhan::tracy_coro
{

//! Process-global monotonically increasing id source. Used both for per-request query ids ("A") and
//! for roots that begin outside any request (e.g. job coroutines).
inline std::uint64_t nextFiberId()
{
	static std::atomic< std::uint64_t > counter { 0 };
	return counter.fetch_add( 1, std::memory_order_relaxed );
}

//! Identity of a coroutine within the fiber timeline, rendered as the name "X.A:B". \p tag is the
//! endpoint/family label ("X"), \p query is a per-request/per-root unique id ("A") shared by every
//! coroutine of one logical operation, and \p depth ("B") is how deeply this coroutine is nested below
//! its root. A zero \p query is the sentinel for "no active context" (this thread is not currently
//! running any coroutine), which makes the next coroutine constructed a fresh root.
struct FiberCtx
{
	const char* tag { nullptr };
	std::uint64_t query { 0 };
	std::int32_t depth { -1 };
};

//! Per-thread context of the coroutine currently executing on this thread. A coroutine reads this at
//! construction time — which happens synchronously inside its parent's body — to learn its parent's
//! query id and depth; the awaiters keep it pointing at the running coroutine (set on resume, cleared
//! on suspend). Request entry seeds it in ServerContext's pre-routing advice.
inline FiberCtx& currentFiberContext()
{
	static thread_local FiberCtx ctx {};
	return ctx;
}

//! Derive the context for a coroutine of family \p prefix from the current thread-local parent. A
//! coroutine with no active parent (query == 0) becomes a fresh root at depth 0, taking the request
//! tag if one is set (jobs have none, so they fall back to the family prefix). Otherwise it inherits
//! the parent's tag and query and sits one level deeper. Called once, at promise construction.
inline FiberCtx makeChildCtx( const char* prefix )
{
	const FiberCtx cur { currentFiberContext() };
	if ( cur.query == 0 ) return FiberCtx { cur.tag ? cur.tag : prefix, nextFiberId() + 1, 0 };
	return FiberCtx { cur.tag, cur.query, cur.depth + 1 };
}

//! Render \p ctx as "X.A:B" into a persistent, stable pointer — call ONCE per coroutine and reuse the
//! pointer for all of that coroutine's enter/leave calls.
//!
//! Tracy's EnterFiber stores the name by POINTER and reads it asynchronously from its background
//! thread, possibly AFTER the coroutine frame that produced the name has been destroyed. So the name
//! must not live in the frame (a std::string promise member is freed on frame destruction ->
//! use-after-free in Tracy). It also must not be a per-instance heap allocation (that leaks one string
//! per coroutine). Instead names live in a fixed-size, never-freed ring of char buffers: the addresses
//! are always valid (no crash) and memory is bounded (no leak). A slot is only reused `pool_size`
//! allocations later — long after Tracy has drained that FiberEnter event — so extreme wraparound can
//! at worst mislabel a fiber, never crash. Each call returns a DISTINCT slot, so every coroutine is a
//! distinct Tracy fiber even when two share the same "X.A:B" text (e.g. same-depth siblings): identical
//! labels are fine, but a shared pointer would make Tracy believe one fiber runs on two threads at once.
inline const char* internFiberNameFor( const FiberCtx& ctx )
{
	static constexpr std::size_t pool_size { 16384 };
	static constexpr std::size_t max_len { 128 };
	static char pool[ pool_size ][ max_len ] {};
	static std::atomic< std::uint64_t > next { 0 };

	const auto slot { next.fetch_add( 1, std::memory_order_relaxed ) % pool_size };
	std::string name;
	if ( ctx.tag )
		name = std::string( ctx.tag ) + '.' + std::to_string( ctx.query ) + ':' + std::to_string( ctx.depth );
	else
		name = std::to_string( ctx.query ) + ':' + std::to_string( ctx.depth );
	const std::size_t n { name.size() < max_len ? name.size() : max_len - 1 };
	std::memcpy( pool[ slot ], name.data(), n );
	pool[ slot ][ n ] = '\0';
	return pool[ slot ];
}

#ifdef TRACY_ENABLE

} // namespace idhan::tracy_coro

	#include <coroutine>
	#include <utility>

namespace idhan::tracy_coro
{

//! Forwarding shims over Tracy's fiber API. Declared (not defined) here on purpose: this header is
//! pulled in almost everywhere via the drogon coroutine shim, so it must NOT include
//! <tracy/Tracy.hpp> — that drags Tracy's heavy client headers (tracy_concurrentqueue.h, etc.) into
//! every TU and collides with system/Qt macros (e.g. BLOCK_SIZE from <linux/*>). The definitions
//! live in exactly one TU, CoroFiber.cpp, which is the only place that includes <tracy/Tracy.hpp>.
void fiberEnter( const char* name ) noexcept;
void fiberLeave() noexcept;

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
	// Stored by reference, never by value: the co_await operand (and its temporaries) is guaranteed
	// to outlive the suspension, so we can bind to it without moving. This is required — drogon's
	// WhenAllAwaiter holds a std::atomic and is immovable, so a by-value member would fail to compile.
	Awaitable&& m_awaitable;
	const char* m_name;
	FiberCtx m_ctx;
	decltype( asAwaiter( std::declval< Awaitable& >() ) ) m_awaiter;

	FiberAwaiter( Awaitable&& awaitable, const char* name, const FiberCtx ctx ) :
	  m_awaitable( std::forward< Awaitable >( awaitable ) ),
	  m_name( name ),
	  m_ctx( ctx ),
	  m_awaiter( asAwaiter( m_awaitable ) )
	{}

	bool await_ready() { return m_awaiter.await_ready(); }

	template < typename Handle >
	auto await_suspend( Handle handle )
	{
		fiberLeave();
		// Clear to the root sentinel so any coroutine constructed while this one is suspended (or any
		// unrelated work the thread picks up) is treated as a fresh root, not our child.
		currentFiberContext() = FiberCtx {};
		return m_awaiter.await_suspend( handle );
	}

	decltype( auto ) await_resume()
	{
		// Re-establish our context so children we construct after resuming read us as their parent.
		currentFiberContext() = m_ctx;
		fiberEnter( m_name );
		return m_awaiter.await_resume();
	}
};

//! Initial awaiter: suspends (lazy body), and on first resume enters the fiber so the body's own
//! zones attribute to this coroutine's fiber. Establishes this coroutine's context so the children it
//! constructs inherit its query id and sit one level deeper.
struct FiberInitialAwaiter
{
	const char* m_name;
	FiberCtx m_ctx;

	bool await_ready() const noexcept { return false; }

	void await_suspend( std::coroutine_handle<> ) const noexcept {}

	void await_resume() const noexcept
	{
		currentFiberContext() = m_ctx;
		fiberEnter( m_name );
	}
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
		fiberLeave();
		currentFiberContext() = FiberCtx {};
		return m_inner.await_suspend( handle );
	}

	void await_resume() noexcept { m_inner.await_resume(); }
};

#endif // TRACY_ENABLE

} // namespace idhan::tracy_coro
