//
// Created by kj16609 on 6/23/23.
//

#pragma once

#include <qpoint.h>
#include <sqlite3.h>
#include <string>

#include "FunctionDecomp.hpp"
#include "binders.hpp"
#include "extractors.hpp"

namespace idhan::hydrus
{

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

	Binder( sqlite3* ptr, const std::string_view sql );

	template < typename T >
	// Why was this here? What reason did I put it here for?
	//requires( !std::is_same_v< std::remove_reference_t< T >, std::string > )
	Binder& operator<<( T t )
	{
		if ( param_counter > max_param_count )
		{
			throw std::runtime_error(
				std::format(
					"param_counter > param_count = {} > {} for query \"{}\"",
					param_counter,
					sqlite3_bind_parameter_count( stmt ),
					std::string( sqlite3_sql( stmt ) ) ) );
		}

		switch ( bindParameter< std::remove_reference_t< T > >( stmt, std::move( t ), ++param_counter ) )
		{
			case SQLITE_OK:
				break;
			default:
				{
					throw std::runtime_error(
						std::format(
							"Failed to bind to \"{}\": Reason: \"{}\"", sqlite3_sql( stmt ), sqlite3_errmsg( ptr ) ) );
				}
		}

		return *this;
	}

	// Feed into value directly
	template < typename T >
		requires( ( !is_optional< T > ) && (!is_tuple< T >))
	void operator>>( T& t )
	{
		std::optional< std::tuple< T > > tpl;

		executeQuery( tpl );

		if ( tpl )
			t = std::move( std::get< 0, T >( tpl.value() ) );
		else
			throw std::runtime_error( std::format( "No rows returned for query \"{}\"", sqlite3_sql( stmt ) ) );
	}

	// Feed output into optional
	template < typename T >
		requires( !is_optional< T > && (!is_tuple< T >))
	void operator>>( std::optional< T >& t )
	{
		std::optional< std::tuple< T > > tpl;

		executeQuery( tpl );

		if ( tpl )
			t = std::move( std::get< 0, T >( tpl.value() ) );
		else
			t = std::nullopt;
	}

	// Call function using output
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

	// Feed output into tuple
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

		return;
	}

  private:

	template < typename... Ts >
		requires( !( is_optional< Ts > || ... ) && !( is_tuple< Ts > || ... ) )
	void executeQuery( std::optional< std::tuple< Ts... > >& tpl_opt )
	{
		if ( param_counter != max_param_count )
			throw std::runtime_error(
				std::format(
					"Not enough parameters given for query! Given {}, Expected {}. param_counter != max_param_count = {} != {} for query \"{}\"",
					param_counter,
					max_param_count,
					param_counter,
					max_param_count,
					std::string_view( sqlite3_sql(
						stmt ) ) ) ); // String view is safe here since the string is owned by sqlite3 and not freed until the statement is finalized

		ran = true;

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
	}

  public:

	~Binder();
};
} // namespace idhan::hydrus
