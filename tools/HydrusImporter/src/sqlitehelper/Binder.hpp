#pragma once

#include <fgl/defines.hpp>

#include <qpoint.h>
#include <sqlite3.h>
#include <string>

#include "FunctionDecomp.hpp"
#include "binders.hpp"
#include "extractors.hpp"

namespace idhan::hydrus
{

//! Fluent SQLite statement wrapper for the Hydrus import: bind parameters with operator<<, then
//! execute and consume results with operator>> (into a value, a std::optional, a tuple, or a per-row
//! callback). \note Unrelated to the server's Drogon SqlBinder; this operates directly on sqlite3.
class Binder
{
	sqlite3* ptr;
	sqlite3_stmt* stmt { nullptr };
	int param_counter { 0 };
	int max_param_count { 0 };
	bool ran { false };

	Q_DISABLE_COPY_MOVE( Binder )

  public:

	Binder() = delete;

	[[nodiscard]] Binder( sqlite3* ptr, std::string_view sql );

	//! Binds \p t as the next positional parameter. \throws std::runtime_error if too many parameters
	//! are supplied or the SQLite bind fails.
	template < typename T >
	Binder& operator<<( T&& t )
	{
		if ( param_counter > max_param_count )
		{
			FGL_ASSERT( stmt != nullptr, "stmt was nullptr when checking param_counter" );
			throw std::runtime_error(
				std::format(
					"param_counter > param_count = {} > {} for query \"{}\"",
					param_counter,
					sqlite3_bind_parameter_count( stmt ),
					std::string_view( sqlite3_sql( stmt ) ) ) );
		}

		FGL_ASSERT( ptr != nullptr, "Database pointer was null" );
		FGL_ASSERT( stmt != nullptr, "Statement was null during parameter binding" );

		if ( const auto ret =
		         bindParameter< std::remove_reference_t< T > >( stmt, std::forward< T >( t ), ++param_counter );
		     ret != SQLITE_OK )
		{
			throw std::runtime_error(
				std::format( "Failed to bind to \"{}\": Reason: \"{}\"", sqlite3_sql( stmt ), sqlite3_errmsg( ptr ) ) );
		}

		return *this;
	}

	template < typename T >
		requires( ( !is_optional< T > ) && (!is_tuple< T >))
	void operator>>( T& t )
	{
		std::optional< std::tuple< T > > tpl;

		executeQuery( tpl );

		if ( tpl )
			t = std::move( std::get< 0 >( tpl.value() ) );
		else
			throw std::runtime_error( std::format( "No rows returned for query \"{}\"", sqlite3_sql( stmt ) ) );
	}

	template < typename T >
		requires( !is_optional< T > && (!is_tuple< T >))
	void operator>>( std::optional< T >& t )
	{
		std::optional< std::tuple< T > > tpl;

		executeQuery( tpl );

		if ( tpl )
			t = std::move( std::get< 0 >( tpl.value() ) );
		else
			t = std::nullopt;
	}

	template < typename Function >
		requires( ( !is_optional< Function > ) && (!is_tuple< Function >))
	void operator>>( Function&& func )
	{
		using FuncArgs = FunctionDecomp< Function >;
		using Tpl = typename FuncArgs::ArgTuple;

		std::optional< Tpl > opt_tpl { std::nullopt };
		executeQuery( opt_tpl );

		while ( opt_tpl )
		{
			std::apply( func, std::move( opt_tpl.value() ) );
			executeQuery( opt_tpl );
		}
	}

	template < typename... Ts >
		requires( !( is_optional< Ts > && ... ) ) && ( !( is_tuple< Ts > && ... ) )
	void operator>>( std::tuple< Ts... >& tpl )
	{
		ran = true;

		std::optional< std::tuple< Ts... > > opt_tpl { std::nullopt };
		executeQuery< Ts... >( opt_tpl );

		if ( opt_tpl )
			tpl = std::move( opt_tpl.value() );
		else
			throw std::runtime_error( "No rows returned for query" );
	}

  private:

	template < typename... Ts >
		requires( !( is_optional< Ts > || ... ) && !( is_tuple< Ts > || ... ) )
	void executeQuery( std::optional< std::tuple< Ts... > >& tpl_opt )
	{
		if ( param_counter != max_param_count )
			throw std::runtime_error(
				std::format(
					"Not enough parameters given for query! Given {}, Expected {} for query \"{}\"",
					param_counter,
					max_param_count,
					std::string_view( sqlite3_sql( stmt ) ) ) );

		ran = true;

		FGL_ASSERT( stmt != nullptr, "stmt was nullptr before executing query" );
		if ( stmt == nullptr ) throw std::runtime_error( "stmt was nullptr" );

		const auto step_ret { sqlite3_step( stmt ) };

		switch ( step_ret )
		{
			case SQLITE_ROW:
				[[likely]]
				{
					if constexpr ( sizeof...( Ts ) > 0 )
					{
						std::tuple< Ts... > tpl { extractRow< Ts... >( stmt ) };
						tpl_opt = std::move( tpl );
						return;
					}
					else
					{
						throw std::runtime_error(
							std::format(
								"No rows were expected but rows were returned for query: \"{}\". Is this intentional?",
								sqlite3_expanded_sql( stmt ) ) );
					}
				}
			case SQLITE_DONE:
				{
					//Help hint to the compiler that it shouldn't keep an empty tuple around
					if constexpr ( sizeof...( Ts ) > 0 ) tpl_opt = std::nullopt;
					return;
				}
			default:
				[[fallthrough]];
			case SQLITE_MISUSE:
				[[fallthrough]];
			case SQLITE_BUSY:
				[[fallthrough]];
			case SQLITE_ERROR:
				{
					throw std::runtime_error(
						std::format(
							"DB: Query error: \"{}\", Query: \"{}\"",
							sqlite3_errmsg( ptr ),
							sqlite3_expanded_sql( stmt ) ) );
				}
		}
	}

  public:

	~Binder();
};

} // namespace idhan::hydrus
