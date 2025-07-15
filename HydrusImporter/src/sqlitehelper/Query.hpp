//
// Created by kj16609 on 7/12/25.
//
#pragma once
#include <coroutine>
#include <cstring>
#include <exception>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>

#include "TransactionBaseCoro.hpp"
#include "binders.hpp"
#include "extractors.hpp"
#include "fgl/defines.hpp"
#include "idhan/logging/logger.hpp"

namespace idhan::hydrus
{

template < typename... TArgs >
struct RowGenerator
{
	using TupleStore = std::tuple< TArgs... >;

	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	struct Empty
	{};

	struct promise_type
	{
		std::optional< TupleStore > m_tuple { std::nullopt };
		std::exception_ptr m_exception { nullptr };

		RowGenerator get_return_object() { return { handle_type::from_promise( *this ) }; }

		// Initial should never suspend so we can setup the sqlite3 stuff
		static std::suspend_never initial_suspend() noexcept { return {}; }

		static std::suspend_always final_suspend() noexcept { return {}; }

		void unhandled_exception() { m_exception = std::current_exception(); }

		void return_void() {}

		std::suspend_always yield_value( Empty ) { return {}; }

		std::suspend_always yield_value( TupleStore&& tuple )
		{
			m_tuple = std::move( tuple );
			return {};
		}
	};

  private:

	handle_type m_h;

  public:

	bool done() const { return m_h.done(); }

	void operator()() const
	{
		if ( m_h.done() ) throw std::runtime_error( "Query has reached unexpected end" );

		m_h.resume();

		if ( m_h.promise().m_exception ) std::rethrow_exception( m_h.promise().m_exception );
	}

	TupleStore operator*() const { return m_h.promise().m_tuple.value(); }

	RowGenerator( handle_type h ) : m_h( h ) {}

	~RowGenerator() { m_h.destroy(); }
};

struct StmtDeleter
{
	void operator()( sqlite3_stmt* stmt ) const
	{
		if ( stmt != nullptr ) sqlite3_finalize( stmt );
	}
};

std::unique_ptr< sqlite3_stmt, StmtDeleter > prepareStatement( TransactionBaseCoro tr, std::string_view sql );

template < typename... TArgs >
RowGenerator< TArgs... >
	buildQuery( TransactionBaseCoro tr, std::unique_ptr< sqlite3_stmt, StmtDeleter >& stmt_holder, auto... sql_args )
{
	// Need to ensure that the stmt is owned outside the coroutine because sqlite3_finalize will break the final co_return because it will go out of scope here.
	auto stmt { stmt_holder.get() };

	constexpr auto sql_args_count { sizeof...( sql_args ) };
	if ( const auto expected_bind_count = sqlite3_bind_parameter_count( stmt ); expected_bind_count != sql_args_count )
	{
		throw std::runtime_error(
			std::format(
				"DB: Failed to bind parameters, Expected: {}, Actual: {}", sql_args_count, expected_bind_count ) );
	}

	switch ( const auto bind_result = bindParameters( stmt, sql_args... ); bind_result )
	{
		case SQLITE_OK:
			break;
		case SQLITE_RANGE: // Second parameter to sqlite3_bind out of range
			[[fallthrough]];
		default:
			throw std::runtime_error(
				std::format(
					"Failed to bind to \"{}\": Reason: \"{}\"", sqlite3_sql( stmt ), sqlite3_errmsg( tr.db() ) ) );
	}

	int step_ret { 0 };

	do {
		step_ret = sqlite3_step( stmt );
		switch ( step_ret )
		{
			default:
				throw std::runtime_error( "Invalid result from sqlite3_step" );
			case SQLITE_ROW:
				co_yield extractRow< TArgs... >( stmt );
				break;
			case SQLITE_DONE:
				co_return;
				break;
		}
	}
	while ( step_ret == SQLITE_ROW );

	throw std::runtime_error( "Invalid return result from sqlite3_step" );

	FGL_UNREACHABLE();
}

template < typename... TArgs >
class Query
{
	std::unique_ptr< sqlite3_stmt, StmtDeleter > m_stmt_ptr;
	RowGenerator< TArgs... > m_generator;

  public:

	using TupleStore = decltype( m_generator )::TupleStore;

	Query( const TransactionBaseCoro tr, const std::string_view sql, auto... sql_args ) :
	  m_stmt_ptr( prepareStatement( tr, sql ) ),
	  m_generator( buildQuery< TArgs... >( tr, m_stmt_ptr, sql_args... ) )
	{}

	struct It
	{
		Query& query {};

		const Query::It& operator++() const
		{
			query.m_generator();
			return *this;
		}

		bool operator==( const std::unreachable_sentinel_t ) const { return query.m_generator.done(); }

		TupleStore operator*() const { return *query.m_generator; }
	};

	// used when expecting only 1 row
	TupleStore operator*() const { return *m_generator; }

	// Begin operator (Returns TupleStore)
	It begin() { return It { *this }; }

	// End operator (Returns an non-existant end)
	std::unreachable_sentinel_t end() const { return {}; }

	Query() = default;
};

} // namespace idhan::hydrus