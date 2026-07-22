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
		fiberLeave();
		return m_awaiter.await_suspend( handle );
	}

	decltype( auto ) await_resume()
	{
		fiberEnter( m_name );
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

	void await_resume() const noexcept { fiberEnter( m_name ); }
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
		return m_inner.await_suspend( handle );
	}

	void await_resume() noexcept { m_inner.await_resume(); }
};

#endif // TRACY_ENABLE

} // namespace idhan::tracy_coro
