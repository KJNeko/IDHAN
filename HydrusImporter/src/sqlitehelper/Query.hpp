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

namespace idhan::hydrus
{

template < typename... TArgs >
struct RowGenerator
{
	using TupleStore = std::tuple< TArgs... >;

	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	struct promise_type
	{
		std::optional< TupleStore > m_tuple { std::nullopt };
		std::exception_ptr m_exception { nullptr };
		std::size_t rows_processed { 0 };

		RowGenerator get_return_object() { return { handle_type::from_promise( *this ) }; }

		std::suspend_always initial_suspend() noexcept { return {}; }

		std::suspend_always final_suspend() noexcept { return {}; }

		void unhandled_exception() { m_exception = std::current_exception(); }

		void return_value( TupleStore&& tuple ) { m_tuple = std::move( tuple ); }

		// void return_void() { m_tuple = std::nullopt; }

		std::suspend_always yield_value( TupleStore&& tuple )
		{
			m_tuple = std::move( tuple );
			return {};
		}
	};

	handle_type m_h;

	const TupleStore& operator()() const
	{
		if ( m_h.done() ) throw std::runtime_error( "Query has reached end" );

		if ( m_h.promise().m_exception ) std::rethrow_exception( m_h.promise().m_exception );
		m_h.resume();
		if ( m_h.promise().m_tuple.has_value() )
		{
			return m_h.promise().m_tuple.value();
			m_h.promise().rows_processed += 1;
		}

		throw std::runtime_error( "Reached end of SQL statement with invalid state" );
	}

	RowGenerator( handle_type h ) : m_h( h ) { m_h.resume(); }

	~RowGenerator() { m_h.destroy(); }
};

template < typename... TArgs >
RowGenerator< TArgs... > buildQuery( TransactionBaseCoro tr, std::string_view sql, auto... sql_args )
{
	using TupleStore = RowGenerator< TArgs... >::TupleStore;
	constexpr auto sql_args_count { sizeof...( sql_args ) };
	sqlite3_stmt* stmt { nullptr };

	const char* unused { nullptr };
	const auto prepare_ret {
		sqlite3_prepare_v2( tr.db(), sql.data(), static_cast< int >( sql.size() + 1 ), &stmt, &unused )
	};

	struct StmtDeleter
	{
		void operator()( sqlite3_stmt* stmt ) const
		{
			if ( stmt != nullptr ) sqlite3_finalize( stmt );
		}
	};

	std::unique_ptr< sqlite3_stmt, StmtDeleter > stmt_ptr { stmt };

	if ( unused != nullptr && strlen( unused ) > 0 )
	{
		const std::string_view leftovers { unused };
		auto itter { leftovers.begin() };
		while ( itter != leftovers.end() )
		{
			if ( *itter == '\n' || *itter == '\t' )
			{
				++itter;
				continue;
			}

			throw std::runtime_error( std::format( "Query had unused portions of the input. Unused: \"{}\"", unused ) );
		}
	}

	if ( stmt == nullptr )
		throw std::runtime_error( std::format( "Failed to prepare stmt, {}", sqlite3_errmsg( tr.db() ) ) );

	if ( prepare_ret != SQLITE_OK )
	{
		throw std::runtime_error(
			std::format( "DB: Failed to prepare statement: \"{}\", Reason: \"{}\"", sql, sqlite3_errmsg( tr.db() ) ) );
	}

	if ( const auto expected_bind_count = sqlite3_bind_parameter_count( stmt ); expected_bind_count != sql_args_count )
	{
		throw std::runtime_error(
			std::format(
				"DB: Failed to bind parameters, Expected: {}, Actual: {}", sql_args_count, expected_bind_count ) );
	}

	switch ( bindParameters( stmt, sql_args... ) )
	{
		case SQLITE_OK:
			break;
		default:
			throw std::runtime_error(
				std::format(
					"Failed to bind to \"{}\": Reason: \"{}\"", sqlite3_sql( stmt ), sqlite3_errmsg( tr.db() ) ) );
	}

	int step_ret = sqlite3_step( stmt );

	while ( step_ret == SQLITE_ROW )
	{
		TupleStore tuple { extractRow< TArgs... >( stmt ) };
		static_assert( std::is_move_constructible_v< TupleStore >, "Tuple must be moveable" );

		step_ret =
			sqlite3_step( stmt ); // if this is SQLITE_DONE then we are finished, Extracting this step will cause issues

		if ( step_ret == SQLITE_DONE )
		{
			co_return std::move( tuple );
		}

		co_yield std::move( tuple );
	}

	FGL_UNREACHABLE();
}

template < typename... TArgs >
class Query
{
	RowGenerator< TArgs... > m_generator;

  public:

	using TupleStore = decltype( m_generator )::TupleStore;

	Query( TransactionBaseCoro tr, std::string_view sql, auto... sql_args ) :
	  m_generator( buildQuery< TArgs... >( tr, sql, sql_args... ) )
	{}

	struct It
	{
		Query& query {};

		const Query::It& operator++() const { return *this; }

		bool operator==( const std::unreachable_sentinel_t ) const { return query.m_generator.m_h.done(); }

		const TupleStore& operator*() const { return query.m_generator(); }
	};

	// Begin operator (Returns TupleStore)
	It begin() { return It { *this }; }

	// End operator (Returns an non-existant end)
	std::unreachable_sentinel_t end() const { return {}; }

	Query() = default;
};

} // namespace idhan::hydrus