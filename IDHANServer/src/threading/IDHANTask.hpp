#pragma once
#include <drogon/utils/coroutine.h>

#include <coroutine>

namespace idhan
{

//! IDHAN's primary coroutine return type. Like drogon::Task it is lazy: initial_suspend is
//! suspend_always, so the body does not run until the task is awaited.
//! \warning Because of that laziness, never store a capturing-lambda coroutine to await later (e.g.
//!          via drogon::when_all): the closure is destroyed before the body runs, leaving the captures
//!          dangling. Use a captureless lambda and pass all state as parameters (copied into the frame).
//! \tparam T The co_returned value type; a void specialisation is provided below.
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

		static auto final_suspend() noexcept { return drogon::final_awaiter {}; }

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

	auto operator co_await() const noexcept { return drogon::task_awaiter< promise_type >( coro_ ); }

	handle_type coro_;
};

//! void specialisation of IDHANTask, for coroutines that co_return nothing.
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

		static auto final_suspend() noexcept { return drogon::final_awaiter {}; }

		void unhandled_exception() { exception_ = std::current_exception(); }

		void result() const
		{
			if ( exception_ != nullptr ) std::rethrow_exception( exception_ );
		}

		void setContinuation( const std::coroutine_handle<> handle ) { continuation_ = handle; }

		std::exception_ptr exception_ {};
		std::coroutine_handle<> continuation_ { std::noop_coroutine() };
	};

	auto operator co_await() const noexcept { return drogon::task_awaiter< promise_type >( coro_ ); }

	handle_type coro_;
};

} // namespace idhan
