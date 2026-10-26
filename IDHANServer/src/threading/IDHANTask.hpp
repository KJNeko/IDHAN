#pragma once
#include <cassert>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace idhan
{

template < typename Promise >
struct IDHANTaskFinalAwaiter
{
	bool await_ready() const noexcept { return false; }

	void await_suspend( std::coroutine_handle< Promise > handle ) const noexcept
	{
		handle.promise().continuation_.resume();
	}

	void await_resume() const noexcept {}
};

template < typename Promise >
struct IDHANTaskAwaiter
{
	std::coroutine_handle< Promise > handle;

	bool await_ready() const noexcept { return handle.done(); }

	void await_suspend( const std::coroutine_handle<> continuation ) const
	{
		handle.promise().setContinuation( continuation );
		handle.resume();
	}

	decltype( auto ) await_resume() const { return std::move( handle.promise() ).result(); }
};

//! Lazy coroutine task. Do not store capturing-lambda coroutines to await later; the closure can die
//! before the body runs.
template < typename T = void >
struct [[nodiscard]] IDHANTask
{
	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	IDHANTask( handle_type h ) : coro_( h ) {}

	IDHANTask( const IDHANTask& ) = delete;

	IDHANTask( IDHANTask&& other ) noexcept
	{
		coro_ = other.coro_;
		other.coro_ = nullptr;
	}

	~IDHANTask()
	{
		if ( coro_ ) coro_.destroy();
	}

	IDHANTask& operator=( const IDHANTask& ) = delete;

	IDHANTask& operator=( IDHANTask&& other ) noexcept
	{
		if ( std::addressof( other ) == this ) return *this;
		if ( coro_ ) coro_.destroy();

		coro_ = other.coro_;
		other.coro_ = nullptr;
		return *this;
	}

	struct promise_type
	{
		IDHANTask< T > get_return_object() { return IDHANTask< T > { handle_type::from_promise( *this ) }; }

		static std::suspend_always initial_suspend() { return {}; }

		void return_value( const T& v ) { value = v; }

		void return_value( T&& v ) { value = std::move( v ); }

		static auto final_suspend() noexcept { return IDHANTaskFinalAwaiter< promise_type > {}; }

		void unhandled_exception() { exception_ = std::current_exception(); }

		T&& result() &&
		{
			if ( exception_ != nullptr ) std::rethrow_exception( exception_ );
			assert( value.has_value() == true );
			return std::move( value.value() );
		}

		T& result() &
		{
			if ( exception_ != nullptr ) std::rethrow_exception( exception_ );
			assert( value.has_value() == true );
			return value.value();
		}

		void setContinuation( const std::coroutine_handle<> handle ) { continuation_ = handle; }

		std::optional< T > value {};
		std::exception_ptr exception_ {};
		std::coroutine_handle<> continuation_ { std::noop_coroutine() };
	};

	auto operator co_await() const noexcept { return IDHANTaskAwaiter< promise_type > { coro_ }; }

	handle_type coro_;
};

template <>
struct [[nodiscard]] IDHANTask< void >
{
	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	IDHANTask( const handle_type handle ) : coro_( handle ) {}

	IDHANTask( const IDHANTask& ) = delete;

	IDHANTask( IDHANTask&& other ) noexcept : coro_( other.coro_ ) { other.coro_ = nullptr; }

	~IDHANTask()
	{
		if ( coro_ ) coro_.destroy();
	}

	IDHANTask& operator=( const IDHANTask& ) = delete;

	IDHANTask& operator=( IDHANTask&& other ) noexcept
	{
		if ( std::addressof( other ) == this ) return *this;
		if ( coro_ ) coro_.destroy();

		coro_ = other.coro_;
		other.coro_ = nullptr;
		return *this;
	}

	struct promise_type
	{
		IDHANTask<> get_return_object() { return IDHANTask<> { handle_type::from_promise( *this ) }; }

		static std::suspend_always initial_suspend() { return {}; }

		void return_void() {}

		static auto final_suspend() noexcept { return IDHANTaskFinalAwaiter< promise_type > {}; }

		void unhandled_exception() { exception_ = std::current_exception(); }

		void result() const
		{
			if ( exception_ != nullptr ) std::rethrow_exception( exception_ );
		}

		void setContinuation( const std::coroutine_handle<> handle ) { continuation_ = handle; }

		std::exception_ptr exception_ {};
		std::coroutine_handle<> continuation_ { std::noop_coroutine() };
	};

	auto operator co_await() const noexcept { return IDHANTaskAwaiter< promise_type > { coro_ }; }

	handle_type coro_;
};

} // namespace idhan
