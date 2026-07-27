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
//
// Task<T> and Task<void> share everything except the promise's value storage and the Awaiter's
// return type. The shared coroutine-frame ownership (construct from handle, destroy on destruction,
// move-only with destroy-before-take on move-assign) lives once in detail::TaskBase; the promise
// types are hoisted out of the class bodies into detail::TaskPromise because a class cannot inherit
// from something defined in terms of its own nested promise_type (promise_type would have to be
// complete before the class is).
#pragma once

#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

#include "fgl/defines.hpp"

namespace idhan::coro
{

template < typename T = void >
class Task;

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

//! Promise type for Task<T>. get_return_object() is declared here and defined out of line, after
//! Task<T> is a complete type, since it returns Task<T> by value.
template < typename T >
struct TaskPromise
{
	std::optional< T > m_value {};
	std::exception_ptr m_exception {};
	std::coroutine_handle<> m_continuation { std::noop_coroutine() };

	Task< T > get_return_object() noexcept;

	static std::suspend_always initial_suspend() noexcept { return {}; }

	static FinalAwaiter final_suspend() noexcept { return {}; }

	void return_value( T value ) { m_value.emplace( std::move( value ) ); }

	void unhandled_exception() noexcept { m_exception = std::current_exception(); }

	T&& result() &&
	{
		if ( m_exception ) std::rethrow_exception( m_exception );
		assert( m_value.has_value() && "Task completed without a value and without an exception" );
		return std::move( *m_value );
	}
};

//! void specialisation: no value slot, return_void instead of return_value, result() returns void.
template <>
struct TaskPromise< void >
{
	std::exception_ptr m_exception {};
	std::coroutine_handle<> m_continuation { std::noop_coroutine() };

	Task< void > get_return_object() noexcept;

	static std::suspend_always initial_suspend() noexcept { return {}; }

	static FinalAwaiter final_suspend() noexcept { return {}; }

	void return_void() const noexcept {}

	void unhandled_exception() noexcept { m_exception = std::current_exception(); }

	void result() &&
	{
		if ( m_exception ) std::rethrow_exception( m_exception );
	}
};

//! Owns the coroutine-frame handle. This is the one place the construct/destroy/move logic exists;
//! Task<T> and Task<void> each only add the promise-specific Awaiter and operator co_await.
template < typename Promise >
class TaskBase
{
  public:

	using handle_type = std::coroutine_handle< Promise >;

	explicit TaskBase( const handle_type handle ) noexcept : m_coro( handle ) {}

	TaskBase() = delete;
	FGL_DELETE_COPY( TaskBase );

	TaskBase( TaskBase&& other ) noexcept : m_coro( std::exchange( other.m_coro, {} ) ) {}

	TaskBase& operator=( TaskBase&& other ) noexcept
	{
		if ( this != &other )
		{
			if ( m_coro ) m_coro.destroy();
			m_coro = std::exchange( other.m_coro, {} );
		}
		return *this;
	}

	~TaskBase()
	{
		if ( m_coro ) m_coro.destroy();
	}

  protected:

	handle_type m_coro {};
};

} // namespace detail

//! Lazy, move-only coroutine return type with no drogon/trantor dependency.
//! \warning Like IDHANTask, this is lazy. Never build one from a capturing lambda and store it to
//!          await later: the closure is destroyed before the body runs, leaving the captures
//!          dangling. Use a captureless lambda or a free function and pass state as parameters,
//!          which are copied into the coroutine frame.
//! \tparam T The co_returned value type. A void specialisation follows.
template < typename T >
class [[nodiscard]] Task : public detail::TaskBase< detail::TaskPromise< T > >
{
	using Base = detail::TaskBase< detail::TaskPromise< T > >;
	// Base is a dependent base class, so unqualified lookup of m_coro from Awaiter and
	// operator co_await below would not find it without this using-declaration.
	using Base::m_coro;

  public:

	using promise_type = detail::TaskPromise< T >;
	using handle_type = typename Base::handle_type;

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

	explicit Task( const handle_type handle ) noexcept : Base( handle ) {}

	Task() = delete;
	FGL_DELETE_COPY( Task );

	Task( Task&& ) noexcept = default;
	Task& operator=( Task&& ) noexcept = default;
	~Task() = default;

	Awaiter operator co_await() const noexcept
	{
		assert( m_coro && "co_await on a moved-from Task" );
		return Awaiter { m_coro };
	}
};

//! void specialisation, for coroutines that co_return nothing.
template <>
class [[nodiscard]] Task< void > : public detail::TaskBase< detail::TaskPromise< void > >
{
	using Base = detail::TaskBase< detail::TaskPromise< void > >;
	using Base::m_coro;

  public:

	using promise_type = detail::TaskPromise< void >;
	using handle_type = Base::handle_type;

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

	explicit Task( const handle_type handle ) noexcept : Base( handle ) {}

	Task() = delete;
	FGL_DELETE_COPY( Task );

	Task( Task&& ) noexcept = default;
	Task& operator=( Task&& ) noexcept = default;
	~Task() = default;

	Awaiter operator co_await() const noexcept
	{
		assert( m_coro && "co_await on a moved-from Task" );
		return Awaiter { m_coro };
	}
};

namespace detail
{

template < typename T >
inline Task< T > TaskPromise< T >::get_return_object() noexcept
{
	return Task< T >( std::coroutine_handle< TaskPromise >::from_promise( *this ) );
}

inline Task< void > TaskPromise< void >::get_return_object() noexcept
{
	return Task< void >( std::coroutine_handle< TaskPromise< void > >::from_promise( *this ) );
}

} // namespace detail

} // namespace idhan::coro
