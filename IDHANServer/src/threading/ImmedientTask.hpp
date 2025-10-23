//
// Created by kj16609 on 10/22/25.
//
#pragma once
#include <coroutine>
#include <exception>
#include <expected>

#include "drogon/utils/coroutine.h"
#include "logging/log.hpp"

namespace idhan::coro
{

template < typename T = void >
using ImmedientTask = ::drogon::Task<T>;

///*
// template < typename T = void >
// struct ImmedientTask;

/*
using inital_suspend_status = std::suspend_always;

template < typename T >
struct [[nodiscard]] PromiseType
{
	using Type = T;
	std::coroutine_handle<> m_cont {};
	std::optional< T > m_data { std::nullopt };
	std::exception_ptr m_exception {};

	ImmedientTask< T > get_return_object() noexcept
	{
		return { std::coroutine_handle< PromiseType >::from_promise( *this ) };
	}

	static std::suspend_never initial_suspend() noexcept { return {}; }

	void unhandled_exception() { m_exception = std::current_exception(); }

	auto final_suspend() noexcept
	{
		struct awaiter
		{
			bool await_ready() noexcept { return false; }

			void await_resume() noexcept {}

			[[nodiscard]] std::coroutine_handle<> await_suspend( std::coroutine_handle< PromiseType > h ) noexcept
			{
				if ( auto continuation = h.promise().m_cont )
				{
					return continuation;
				}
				return std::noop_coroutine();
			}
		};

		return awaiter {};
	}

	void return_value( const T& value ) noexcept { m_data = value; }
};

template <>
struct [[nodiscard]] PromiseType< void >
{
	using Type = void;
	std::coroutine_handle<> m_cont {};
	std::exception_ptr m_exception {};

	ImmedientTask< void > get_return_object() noexcept;

	static std::suspend_never initial_suspend() noexcept { return {}; }

	void unhandled_exception() { m_exception = std::current_exception(); }

	auto final_suspend() noexcept
	{
		struct awaiter
		{
			bool await_ready() noexcept { return false; }

			void await_resume() noexcept {}

			[[nodiscard]] std::coroutine_handle<> await_suspend( std::coroutine_handle< PromiseType > h ) noexcept
			{
				if ( auto continuation = h.promise().m_cont )
				{
					return continuation;
				}
				return std::noop_coroutine();
			}
		};

		return awaiter {};
	}

	void return_void() noexcept {}
};

template < typename PromiseType >
struct Awaiter
{
	std::coroutine_handle< PromiseType > m_h;
	using T = typename PromiseType::Type;

	bool await_ready() noexcept { return !m_h || m_h.done(); }

	std::coroutine_handle<> await_suspend( std::coroutine_handle<> awaiting ) noexcept
	{
		if ( !m_h )
		{
			log::critical( "Invalid Coro given to Awaiter in Task builder" );
			std::terminate();
		}

		if ( m_h.done() )
		{
			// go back to running the same coro
			return std::noop_coroutine();
		}

		m_h.promise().m_cont = awaiting;
		return m_h;
	}

	T await_resume() noexcept
	{
		auto& promise { m_h.promise() };

		if ( promise.m_exception )
		{
			std::rethrow_exception( promise.m_exception );
		}

		if constexpr ( !std::is_void_v< T > )
		{
			auto& data { promise.m_data };

			if ( !data.has_value() )
			{
				log::critical( "Invalid coroutine. Coro did not have value" );
				std::terminate();
			}

			return std::move( m_h.promise().m_data.value() );
		}
		else
		{
			return;
		}
	}
};

template < typename T >
struct [[nodiscard]] ImmedientTask
{
	using promise_type = PromiseType< T >;
	std::coroutine_handle< promise_type > m_h;

	[[nodiscard]] bool await_ready() const noexcept { return m_h.done(); }

	[[nodiscard]] T await_resume() const noexcept
	{
		FGL_ASSERT( m_h.promise().m_data.has_value(), "Promise did not have value" );
		return m_h.promise().m_data.value();
	}

	void await_suspend( std::coroutine_handle<> caller ) const noexcept { m_h.promise().m_cont = caller; }

	auto operator co_await() noexcept { return Awaiter { m_h }; }

	~ImmedientTask() {}
};

inline ImmedientTask< void > PromiseType< void >::get_return_object() noexcept
{
	return { std::coroutine_handle< PromiseType >::from_promise( *this ) };
}
*/

} // namespace idhan::coro