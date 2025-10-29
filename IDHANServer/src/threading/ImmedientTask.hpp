//
// Created by kj16609 on 10/29/25.
//
#pragma once
#include <coroutine>

namespace idhan
{

template < typename T = void >
struct ImmedientTask
{
	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	ImmedientTask( handle_type h ) : coro_( h ) {}

	ImmedientTask( const ImmedientTask& ) = delete;

	ImmedientTask( ImmedientTask&& other ) noexcept
	{
		coro_ = other.coro_;
		other.coro_ = nullptr;
	}

	~ImmedientTask()
	{
		if ( coro_ ) coro_.destroy();
	}

	ImmedientTask& operator=( const ImmedientTask& ) = delete;

	ImmedientTask& operator=( ImmedientTask&& other ) noexcept
	{
		if ( std::addressof( other ) == this ) return *this;
		if ( coro_ ) coro_.destroy();

		coro_ = other.coro_;
		other.coro_ = nullptr;
		return *this;
	}

	struct promise_type
	{
		ImmedientTask< T > get_return_object() { return ImmedientTask< T > { handle_type::from_promise( *this ) }; }

		std::suspend_never initial_suspend() { return {}; }

		void return_value( const T& v ) { value = v; }

		void return_value( T&& v ) { value = std::move( v ); }

		auto final_suspend() noexcept { return drogon::final_awaiter {}; }

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

		void setContinuation( std::coroutine_handle<> handle ) { continuation_ = handle; }

		std::optional< T > value;
		std::exception_ptr exception_;
		std::coroutine_handle<> continuation_ { std::noop_coroutine() };
	};

	auto operator co_await() const noexcept { return drogon::task_awaiter< promise_type >( coro_ ); }

	handle_type coro_;
};

template <>
struct [[nodiscard]] ImmedientTask< void >
{
	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	ImmedientTask( handle_type handle ) : coro_( handle ) {}

	ImmedientTask( const ImmedientTask& ) = delete;

	ImmedientTask( ImmedientTask&& other ) noexcept
	{
		coro_ = other.coro_;
		other.coro_ = nullptr;
	}

	~ImmedientTask()
	{
		if ( coro_ ) coro_.destroy();
	}

	ImmedientTask& operator=( const ImmedientTask& ) = delete;

	ImmedientTask& operator=( ImmedientTask&& other ) noexcept
	{
		if ( std::addressof( other ) == this ) return *this;
		if ( coro_ ) coro_.destroy();

		coro_ = other.coro_;
		other.coro_ = nullptr;
		return *this;
	}

	struct promise_type
	{
		ImmedientTask<> get_return_object() { return ImmedientTask<> { handle_type::from_promise( *this ) }; }

		std::suspend_never initial_suspend() { return {}; }

		void return_void() {}

		auto final_suspend() noexcept { return drogon::final_awaiter {}; }

		void unhandled_exception() { exception_ = std::current_exception(); }

		void result()
		{
			if ( exception_ != nullptr ) std::rethrow_exception( exception_ );
		}

		void setContinuation( std::coroutine_handle<> handle ) { continuation_ = handle; }

		std::exception_ptr exception_;
		std::coroutine_handle<> continuation_ { std::noop_coroutine() };
	};

	auto operator co_await() const noexcept { return drogon::task_awaiter< promise_type >( coro_ ); }

	handle_type coro_;
};

} // namespace idhan