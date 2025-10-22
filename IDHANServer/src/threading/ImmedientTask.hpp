//
// Created by kj16609 on 10/22/25.
//
#pragma once
#include <coroutine>
#include <exception>

namespace idhan::coro
{

template < typename T = void >
class [[nodiscard]] ImmedientTask
{
  public:

	struct promise_type
	{
		T value {};
		std::exception_ptr exception;

		ImmedientTask get_return_object()
		{
			return ImmedientTask( std::coroutine_handle< promise_type >::from_promise( *this ) );
		}

		static std::suspend_always initial_suspend() { return {}; }

		static std::suspend_always final_suspend() noexcept { return {}; }

		void return_value( T v ) { value = v; }

		void unhandled_exception() { exception = std::current_exception(); }
	};

	using handle_type = std::coroutine_handle< promise_type >;

	ImmedientTask( handle_type h ) : handle( h ) {}

	~ImmedientTask()
	{
		if ( handle ) handle.destroy();
	}

	ImmedientTask( const ImmedientTask& ) = delete;
	ImmedientTask& operator=( const ImmedientTask& ) = delete;

	ImmedientTask( ImmedientTask&& other ) noexcept : handle( other.handle ) { other.handle = nullptr; }

	ImmedientTask& operator=( ImmedientTask&& other ) noexcept
	{
		if ( this != &other )
		{
			if ( handle ) handle.destroy();
			handle = other.handle;
			other.handle = nullptr;
		}
		return *this;
	}

	T get()
	{
		if ( handle.promise().exception ) std::rethrow_exception( handle.promise().exception );
		return handle.promise().value;
	}

	struct awaiter
	{
		bool await_ready() { return true; }

		void await_suspend( std::coroutine_handle<> ) {}

		T await_resume() { return handle.promise().value; }

		handle_type handle;
	};

	auto operator co_await() { return awaiter { handle }; }

  private:

	handle_type handle;
};

} // namespace idhan::coro